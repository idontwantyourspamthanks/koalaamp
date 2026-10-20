// SPDX-License-Identifier: MIT
// interop/vlc — raw libvlc C API decoded-PCM tap (FE-2 spike).

#include "interop/vlc/VlcPcmTap.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace koalaamp::interop::vlc {
namespace {

std::int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

struct VlcPcmTap::StreamState {
    std::shared_ptr<playback::IMediaStreamSource> source;
    std::unique_ptr<playback::IMediaStream> stream; // valid between open and close
};

VlcPcmTap::VlcPcmTap(std::size_t ringFloats) : m_ring(ringFloats) {}

VlcPcmTap::~VlcPcmTap() {
    if (m_mp) {
        libvlc_media_player_stop(m_mp);
    }
    if (m_eventManager) {
        libvlc_event_detach(m_eventManager, libvlc_MediaPlayerEndReached, &VlcPcmTap::cbEvent, this);
        libvlc_event_detach(m_eventManager, libvlc_MediaPlayerEncounteredError, &VlcPcmTap::cbEvent,
                            this);
        libvlc_event_detach(m_eventManager, libvlc_MediaPlayerPlaying, &VlcPcmTap::cbEvent, this);
        libvlc_event_detach(m_eventManager, libvlc_MediaPlayerPaused, &VlcPcmTap::cbEvent, this);
        libvlc_event_detach(m_eventManager, libvlc_MediaPlayerStopped, &VlcPcmTap::cbEvent, this);
        libvlc_event_detach(m_eventManager, libvlc_MediaPlayerLengthChanged, &VlcPcmTap::cbEvent,
                            this);
        m_eventManager = nullptr;
    }
    if (m_mp) {
        libvlc_media_player_release(m_mp);
    }
    if (m_media) {
        libvlc_media_release(m_media);
    }
    // After the media: its callbacks may still run until the player is stopped,
    // and the opaque they receive lives here.
    m_streamState.reset();
    if (m_instance) {
        libvlc_release(m_instance);
    }
}

bool VlcPcmTap::ensureInstance() {
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
        if (!attachEvents()) {
            return false;
        }
    }
    return true;
}

// The media player's event manager lives for the player's lifetime (libvlc
// 3.0.x creates it in libvlc_media_player_new and frees it on release), so we
// attach once, when the player is created, and detach in the destructor.
bool VlcPcmTap::attachEvents() {
    m_eventManager = libvlc_media_player_event_manager(m_mp);
    if (!m_eventManager) {
        return false;
    }
    struct {
        libvlc_event_type_t type;
    } const types[] = {
        {libvlc_MediaPlayerEndReached},
        {libvlc_MediaPlayerEncounteredError},
        {libvlc_MediaPlayerPlaying},
        {libvlc_MediaPlayerPaused},
        {libvlc_MediaPlayerStopped},
        {libvlc_MediaPlayerLengthChanged},
    };
    for (const auto& t : types) {
        if (libvlc_event_attach(m_eventManager, t.type, &VlcPcmTap::cbEvent, this) != 0) {
            return false;
        }
    }
    return true;
}

bool VlcPcmTap::open(const std::string& path) {
    return ensureInstance() && setMediaImpl(path, false);
}

bool VlcPcmTap::openLocation(const std::string& url) {
    return ensureInstance() && setMediaImpl(url, true);
}

bool VlcPcmTap::setMedia(const std::string& path) {
    return m_instance != nullptr && m_mp != nullptr && setMediaImpl(path, false);
}

bool VlcPcmTap::setMediaLocation(const std::string& url) {
    return m_instance != nullptr && m_mp != nullptr && setMediaImpl(url, true);
}

bool VlcPcmTap::setMediaImpl(const std::string& source, bool location) {
    if (!m_instance || !m_mp) {
        return false;
    }
    libvlc_media_t* media = location ? libvlc_media_new_location(m_instance, source.c_str())
                                     : libvlc_media_new_path(m_instance, source.c_str());
    if (!media) {
        return false;
    }
    libvlc_media_player_set_media(m_mp, media);
    if (m_media) {
        libvlc_media_release(m_media);
    }
    m_streamState.reset(); // the previous media may have been callback-backed
    m_media = media;
    // Bump after the switch: events emitted from here on belong to this media,
    // and anything tagged with an older generation is stale (FE-4).
    m_generation.fetch_add(1, std::memory_order_release);
    return true;
}

bool VlcPcmTap::setMediaStream(std::shared_ptr<playback::IMediaStreamSource> source) {
    if (!ensureInstance() || !m_mp || !source) {
        return false;
    }
    auto state = std::make_unique<StreamState>();
    state->source = std::move(source);
    libvlc_media_t* media =
        libvlc_media_new_callbacks(m_instance, &VlcPcmTap::cbStreamOpen, &VlcPcmTap::cbStreamRead,
                                   &VlcPcmTap::cbStreamSeek, &VlcPcmTap::cbStreamClose,
                                   state.get());
    if (!media) {
        return false;
    }
    libvlc_media_player_set_media(m_mp, media);
    if (m_media) {
        libvlc_media_release(m_media);
    }
    // Old media released first, then its opaque: the player was stopped by the
    // caller before the switch, so no callback can still be in flight for it.
    m_streamState = std::move(state);
    m_media = media;
    m_generation.fetch_add(1, std::memory_order_release);
    return true;
}

int VlcPcmTap::cbStreamOpen(void* opaque, void** datap, std::uint64_t* sizep) {
    auto* state = static_cast<StreamState*>(opaque);
    if (!state || !state->source) {
        return -1;
    }
    std::int64_t size = -1;
    state->stream = state->source->open(&size);
    if (!state->stream) {
        return -1;
    }
    // libvlc's contract: UINT64_MAX means "size unknown", not 0.
    if (sizep != nullptr) {
        *sizep = size >= 0 ? static_cast<std::uint64_t>(size) : UINT64_MAX;
    }
    // libvlc hands `datap` to read/seek/close (the callback ABI's private
    // pointer); the media's opaque is the same state, so either route lands
    // here. A null below would make the first read fail without a trace.
    if (datap != nullptr) {
        *datap = state;
    }
    return 0;
}

ssize_t VlcPcmTap::cbStreamRead(void* opaque, unsigned char* buf, std::size_t len) {
    auto* state = static_cast<StreamState*>(opaque);
    if (!state || !state->stream) {
        return -1;
    }
    return static_cast<ssize_t>(state->stream->read(buf, len));
}

int VlcPcmTap::cbStreamSeek(void* opaque, std::uint64_t offset) {
    auto* state = static_cast<StreamState*>(opaque);
    if (!state || !state->stream ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return -1;
    }
    return state->stream->seek(static_cast<std::int64_t>(offset)) ? 0 : -1;
}

void VlcPcmTap::cbStreamClose(void* opaque) {
    auto* state = static_cast<StreamState*>(opaque);
    if (state) {
        state->stream.reset(); // releases the HTTP response, not the source
    }
}

void VlcPcmTap::play() {
    if (m_mp) {
        libvlc_media_player_play(m_mp);
    }
}
void VlcPcmTap::pause() {
    if (m_mp) {
        libvlc_media_player_set_pause(m_mp, 1);
    }
}
void VlcPcmTap::resume() {
    if (m_mp) {
        libvlc_media_player_set_pause(m_mp, 0);
    }
}
void VlcPcmTap::stop() {
    if (m_mp) {
        libvlc_media_player_stop(m_mp);
    }
}

void VlcPcmTap::seekMs(std::int64_t ms) {
    if (m_mp) {
        libvlc_media_player_set_time(m_mp, ms);
    }
}

bool VlcPcmTap::isPlaying() const {
    return m_mp != nullptr && libvlc_media_player_is_playing(m_mp) != 0;
}

std::int64_t VlcPcmTap::timeMs() const {
    return m_mp ? libvlc_media_player_get_time(m_mp) : -1;
}

std::int64_t VlcPcmTap::lengthMs() const {
    return m_mp ? libvlc_media_player_get_length(m_mp) : -1;
}

void VlcPcmTap::setEventListener(TapEventListener listener) {
    m_listener = std::move(listener);
}

void VlcPcmTap::setPcmObserver(TapPcmObserver observer) {
    m_pcmObserver = std::move(observer);
}

void VlcPcmTap::setOutputGain(float linear) {
    m_gain.store(linear, std::memory_order_relaxed);
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
        const float gain = self->m_gain.load(std::memory_order_relaxed);
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const float v = static_cast<float>(in[i]) * scale * gain;
            f[i] = v;
            sum += static_cast<double>(v) * static_cast<double>(v);
        }
        // PCM observer = the DSP-mode insertion seam (docs/spec.md §3.1). It
        // sees the float block post-gain, pre-ring — the same samples the sink
        // renders. Set before play(); no lock is taken on this path.
        if (self->m_pcmObserver) {
            self->m_pcmObserver(f, count, self->m_format.sampleRate.load(std::memory_order_relaxed),
                                channels);
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
    // AU-2: the aout flush is NOT a reason to drop the ring. libvlc flushes
    // both on a seek — where the buffered frames belong to a position the
    // listener has left — *and* at end-of-input, where the buffered frames are
    // the last ~1 s of the track, perfectly valid and still to be rendered.
    // Discarding here (FE-2/FE-4 behaviour) made every natural track end lose
    // that tail (measured: ~1.0 s per boundary), which no amount of prefetching
    // can hide. The ring is cleared only where a discontinuity is *decided*:
    // the core discards on seek/stop/explicit track switch (PlaybackCore), so
    // the natural end can hand the next media over with the tail preserved.
}

void VlcPcmTap::cbDrain(void* data) {
    static_cast<VlcPcmTap*>(data)->m_stats.drains.fetch_add(1, std::memory_order_relaxed);
}

// libvlc thread. Marshal only — never call back into libvlc from here.
void VlcPcmTap::cbEvent(const libvlc_event_t* event, void* data) {
    auto* self = static_cast<VlcPcmTap*>(data);
    if (!self->m_listener || event == nullptr) {
        return;
    }
    // Sampled here, at emit time: the consumer compares it against the current
    // generation to tell a live event from one belonging to a media it has
    // already replaced (see TapEventListener).
    const std::uint64_t generation = self->m_generation.load(std::memory_order_acquire);
    switch (event->type) {
    case libvlc_MediaPlayerEndReached:
        self->m_listener(TapEvent::EndReached, 0, generation);
        break;
    case libvlc_MediaPlayerEncounteredError:
        self->m_listener(TapEvent::EncounteredError, 0, generation);
        break;
    case libvlc_MediaPlayerPlaying:
        self->m_listener(TapEvent::Playing, 0, generation);
        break;
    case libvlc_MediaPlayerPaused:
        self->m_listener(TapEvent::Paused, 0, generation);
        break;
    case libvlc_MediaPlayerStopped:
        self->m_listener(TapEvent::Stopped, 0, generation);
        break;
    case libvlc_MediaPlayerLengthChanged:
        self->m_listener(TapEvent::LengthChanged,
                         event->u.media_player_length_changed.new_length, generation);
        break;
    default:
        break;
    }
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
