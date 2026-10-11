// SPDX-License-Identifier: MIT
// interop/vlc — raw libvlc C API decoded-PCM tap (FE-2 spike).

#include "interop/vlc/VlcPcmTap.hpp"

#include <chrono>
#include <cmath>
#include <cstring>

namespace koalaamp::interop::vlc {
namespace {

std::int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

VlcPcmTap::VlcPcmTap(std::size_t ringFloats) : m_ring(ringFloats) {}

VlcPcmTap::~VlcPcmTap() {
    if (m_mp) {
        libvlc_media_player_stop(m_mp);
        libvlc_media_player_release(m_mp);
    }
    if (m_media) {
        libvlc_media_release(m_media);
    }
    if (m_instance) {
        libvlc_release(m_instance);
    }
}

bool VlcPcmTap::open(const std::string& path) {
    if (!m_instance) {
        // DSP-mode profile (docs/spec.md §3.1): video/SPU off, time-stretch off,
        // hardware decode off, empty user-filter chain to start from.
        static const char* args[] = {
            "--quiet",
            "--no-video",
            "--no-spu",
            "--no-osd",
            "--no-audio-time-stretch",
            "--avcodec-hw=none",
            "--audio-filter=",
        };
        m_instance = libvlc_new(static_cast<int>(std::size(args)), args);
    }
    if (!m_instance) {
        return false;
    }
    if (!m_mp) {
        m_mp = libvlc_media_player_new(m_instance);
        if (!m_mp) {
            return false;
        }
        libvlc_audio_set_callbacks(m_mp, &VlcPcmTap::cbPlay, &VlcPcmTap::cbPause,
                                   &VlcPcmTap::cbResume, &VlcPcmTap::cbFlush, &VlcPcmTap::cbDrain,
                                   this);
        libvlc_audio_set_format_callbacks(m_mp, &VlcPcmTap::cbSetup, nullptr);
        libvlc_media_player_set_equalizer(m_mp, nullptr); // DSP mode: no native EQ
        // amem blocks are codec-sized; 1<<15 floats covers the largest we have
        // seen (FLAC 4608 frames * 2 ch) with room to spare.
        m_scratch.resize(1u << 15);
    }
    return setMedia(path);
}

bool VlcPcmTap::setMedia(const std::string& path) {
    if (!m_instance || !m_mp) {
        return false;
    }
    libvlc_media_t* media = libvlc_media_new_path(m_instance, path.c_str());
    if (!media) {
        return false;
    }
    libvlc_media_player_set_media(m_mp, media);
    if (m_media) {
        libvlc_media_release(m_media);
    }
    m_media = media;
    return true;
}

void VlcPcmTap::play() {
    libvlc_media_player_play(m_mp);
}
void VlcPcmTap::pause() {
    libvlc_media_player_set_pause(m_mp, 1);
}
void VlcPcmTap::stop() {
    libvlc_media_player_stop(m_mp);
}

void VlcPcmTap::seekMs(std::int64_t ms) {
    libvlc_media_player_set_time(m_mp, ms);
}

bool VlcPcmTap::isPlaying() const {
    return libvlc_media_player_is_playing(m_mp) != 0;
}

std::int64_t VlcPcmTap::timeMs() const {
    return libvlc_media_player_get_time(m_mp);
}

void VlcPcmTap::setEqualizerGainDb(double db) {
    libvlc_equalizer_t* eq = libvlc_audio_equalizer_new();
    if (!eq) {
        return;
    }
    const unsigned bands = libvlc_audio_equalizer_get_band_count();
    // VLC's equalizer filter multiplies by EQZ_IN_FACTOR (0.25 = -12 dB) as its
    // baseline (modules/audio_filter/equalizer.c), so a bands-only "+12 dB"
    // preset actually nets ≈ +2.5 dB. Applying the same +12 dB to the preamp
    // compensates that factor, yielding the nominal +12 dB change we assert.
    libvlc_audio_equalizer_set_preamp(eq, static_cast<float>(db));
    for (unsigned i = 0; i < bands; ++i) {
        libvlc_audio_equalizer_set_amp_at_index(eq, static_cast<float>(db), i);
    }
    libvlc_media_player_set_equalizer(m_mp, eq);
    libvlc_audio_equalizer_release(eq); // set_equalizer copies; we drop our ref
    m_nativeEq.store(true, std::memory_order_relaxed);
}

void VlcPcmTap::clearEqualizer() {
    libvlc_media_player_set_equalizer(m_mp, nullptr);
    m_nativeEq.store(false, std::memory_order_relaxed);
}

// libvlc 3.x exposes no getter for the active equalizer, so we track what we
// installed (the toggle the EQ-rule test asserts on).
bool VlcPcmTap::hasNativeEqualizer() const {
    return m_nativeEq.load(std::memory_order_relaxed);
}

void VlcPcmTap::enableTiming(std::size_t capacity) {
    m_cbIntervalsUs.reset(capacity);
}

void VlcPcmTap::armFirstCallback() {
    m_stats.firstCallbackNs.store(0, std::memory_order_relaxed);
    m_armed.store(true, std::memory_order_release);
}

double VlcPcmTap::rmsLevel() const {
    const double ms = m_msEma.load(std::memory_order_relaxed);
    return ms > 0.0 ? std::sqrt(ms) : 0.0;
}

double VlcPcmTap::callbackIntervalP50ms() const {
    return m_cbIntervalsUs.percentile(0.50) / 1000.0;
}
double VlcPcmTap::callbackIntervalP95ms() const {
    return m_cbIntervalsUs.percentile(0.95) / 1000.0;
}

void VlcPcmTap::cbPlay(void* data, const void* samples, unsigned count, std::int64_t pts) {
    auto* self = static_cast<VlcPcmTap*>(data);
    Stats& s = self->m_stats;
    s.callbacks.fetch_add(1, std::memory_order_relaxed);
    s.framesIn.fetch_add(count, std::memory_order_relaxed);

    const std::int64_t now = nowNs();
    const unsigned channels = self->m_format.channels.load(std::memory_order_relaxed);
    if (count != 0 && channels != 0) {
        const std::size_t n = static_cast<std::size_t>(count) * channels;
        const auto* in = static_cast<const std::int16_t*>(samples);
        if (n > self->m_scratch.size()) {
            self->m_scratch.resize(n); // first block only; sized up front in open()
        }
        float* f = self->m_scratch.data();
        constexpr float scale = 1.0f / 32768.0f; // VLC S16N is full-scale 32768
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const float v = static_cast<float>(in[i]) * scale;
            f[i] = v;
            sum += static_cast<double>(v) * static_cast<double>(v);
        }
        const double blockMs = sum / static_cast<double>(n);
        const double prev = self->m_msEma.load(std::memory_order_relaxed);
        self->m_msEma.store(prev + 0.25 * (blockMs - prev), std::memory_order_relaxed);

        // Note (FE-2 spike): blocking here to back-pressure libvlc is NOT
        // viable — this callback is the aout's clock, so blocking it slows
        // delivery below real time (measured: 62 % rate, massive underruns).
        // libvlc self-paces to real time and fills whatever ring it is given,
        // so the ring size alone bounds the latency and the decode-ahead.
        const std::size_t wrote = self->m_ring.write(f, n);
        if (wrote < n) {
            s.framesDropped.fetch_add((n - wrote) / channels, std::memory_order_relaxed);
        }
        if (wrote != 0) {
            self->m_ring.stampAfterWrite(now);
        }
    }

    const std::int64_t prevCb = s.lastCallbackNs.exchange(now, std::memory_order_relaxed);
    if (prevCb != 0 && now > prevCb) {
        const auto deltaUs = static_cast<std::uint64_t>((now - prevCb) / 1000);
        if (deltaUs <= 0xffffffffULL) {
            self->m_cbIntervalsUs.add(static_cast<std::uint32_t>(deltaUs));
        }
    }
    s.lastPtsUs.store(pts, std::memory_order_relaxed);
    if (self->m_armed.exchange(false, std::memory_order_acq_rel)) {
        s.firstCallbackNs.store(now, std::memory_order_release);
    }
}

void VlcPcmTap::cbPause(void* data, std::int64_t) {
    static_cast<VlcPcmTap*>(data)->m_stats.pauses.fetch_add(1, std::memory_order_relaxed);
}

void VlcPcmTap::cbResume(void* data, std::int64_t) {
    static_cast<VlcPcmTap*>(data)->m_stats.resumes.fetch_add(1, std::memory_order_relaxed);
}

void VlcPcmTap::cbFlush(void* data, std::int64_t) {
    auto* self = static_cast<VlcPcmTap*>(data);
    self->m_stats.flushes.fetch_add(1, std::memory_order_relaxed);
    self->m_ring.discardAll();
}

void VlcPcmTap::cbDrain(void* data) {
    static_cast<VlcPcmTap*>(data)->m_stats.drains.fetch_add(1, std::memory_order_relaxed);
}

int VlcPcmTap::cbSetup(void** data, char* format, unsigned* rate, unsigned* channels) {
    if (data != nullptr && *data != nullptr) {
        auto* self = static_cast<VlcPcmTap*>(*data);
        self->m_format.sampleRate.store(48000, std::memory_order_relaxed);
        self->m_format.channels.store(2, std::memory_order_relaxed);
    }
    // VLC 3.0.x's amem audio output accepts S16N only — see
    // modules/audio_output/amem.c Start(): the returned chroma is checked
    // against "S16N" (the variable "amem-format" is a TODO). Requesting FL32
    // makes amem fail with "format not supported". So the callback path is
    // 16-bit interleaved and we convert to float ourselves.
    std::memcpy(format, "S16N", 4);
    format[4] = '\0';
    *rate = 48000;
    *channels = 2;
    return 0;
}

} // namespace koalaamp::interop::vlc
