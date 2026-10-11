// SPDX-License-Identifier: MIT
// interop/vlc — miniaudio playback sink (FE-2 spike).

#include "interop/vlc/MiniAudioSink.hpp"

#include <chrono>
#include <cstring>

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace koalaamp::interop::vlc {

struct MiniAudioSink::Impl {
    ma_context context{};
    ma_device device{};
    bool contextInit{false};
    bool deviceInit{false};
};

MiniAudioSink::MiniAudioSink(FloatRing& ring) : m_ring(ring) {
    m_backlog.reset(1u << 18);
}

MiniAudioSink::~MiniAudioSink() {
    stop();
}

bool MiniAudioSink::start(const SinkConfig& config) {
    stop();
    m_channels = config.channels;
    m_sampleRate = config.sampleRate;
    m_backlog.reset(1u << 18);
    m_residencyUs.reset(1u << 18);
    m_impl = new Impl();

    ma_backend backends[1]{};
    unsigned count = 0;
    switch (config.backend) {
    case SinkBackend::Alsa:
        backends[count++] = ma_backend_alsa;
        break;
    case SinkBackend::Pulse:
        backends[count++] = ma_backend_pulseaudio;
        break;
    case SinkBackend::Auto:
        break;
    }

    ma_context_config contextConfig = ma_context_config_init();
    if (ma_context_init(count ? backends : nullptr, count, &contextConfig, &m_impl->context) !=
        MA_SUCCESS) {
        stop();
        return false;
    }
    m_impl->contextInit = true;
    m_backend = ma_get_backend_name(m_impl->context.backend);

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = config.channels;
    deviceConfig.sampleRate = config.sampleRate;
    deviceConfig.periodSizeInFrames = config.periodFrames;
    deviceConfig.performanceProfile = ma_performance_profile_low_latency;
    deviceConfig.dataCallback = &MiniAudioSink::dataCallback;
    deviceConfig.pUserData = this;

    if (ma_device_init(&m_impl->context, &deviceConfig, &m_impl->device) != MA_SUCCESS) {
        stop();
        return false;
    }
    m_impl->deviceInit = true;
    m_periodFrames = m_impl->device.playback.internalPeriodSizeInFrames;

    if (ma_device_start(&m_impl->device) != MA_SUCCESS) {
        stop();
        return false;
    }
    return true;
}

void MiniAudioSink::stop() {
    if (!m_impl) {
        return;
    }
    if (m_impl->deviceInit) {
        ma_device_stop(&m_impl->device);
        ma_device_uninit(&m_impl->device);
    }
    if (m_impl->contextInit) {
        ma_context_uninit(&m_impl->context);
    }
    delete m_impl;
    m_impl = nullptr;
}

void MiniAudioSink::dataCallback(ma_device* dev, void* output, const void* /*input*/,
                                 unsigned frameCount) {
    auto* self = static_cast<MiniAudioSink*>(dev->pUserData);
    auto* out = static_cast<float*>(output);
    const std::size_t need = static_cast<std::size_t>(frameCount) * self->m_channels;

    // Residency of frames already consumed: how long they waited between the
    // libvlc callback and this device pull.
    const auto now =
        static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count());
    const std::size_t popped = self->m_ring.collectResidency(now, self->m_residencyScratch.data(),
                                                             self->m_residencyScratch.size());
    for (std::size_t i = 0; i < popped; ++i) {
        if (self->m_residencyScratch[i] >= 0) {
            self->m_residencyUs.add(static_cast<std::uint32_t>(self->m_residencyScratch[i] / 1000));
        }
    }

    const std::size_t got = self->m_ring.read(out, need);
    if (got < need) {
        std::memset(out + got, 0, (need - got) * sizeof(float));
        self->m_stats.underruns.fetch_add(1, std::memory_order_relaxed);
        self->m_stats.silenceFrames.fetch_add((need - got) / self->m_channels,
                                              std::memory_order_relaxed);
    }
    self->m_stats.deviceCallbacks.fetch_add(1, std::memory_order_relaxed);
    self->m_stats.framesRendered.fetch_add(frameCount, std::memory_order_relaxed);

    // Backlog left in the ring after serving this callback = the age of the
    // oldest queued frame (the decode-ahead latency), in frames.
    const auto backlogFrames = static_cast<std::uint32_t>(self->m_ring.fill() / self->m_channels);
    self->m_backlog.add(backlogFrames);
}

double MiniAudioSink::backlogP95Ms() const {
    return backlogP95Frames() * 1000.0 / static_cast<double>(m_sampleRate);
}

double MiniAudioSink::backlogMaxMs() const {
    return static_cast<double>(m_backlog.max()) * 1000.0 / static_cast<double>(m_sampleRate);
}

unsigned MiniAudioSink::backlogP95Frames() const {
    return static_cast<unsigned>(m_backlog.percentile(0.95));
}

double MiniAudioSink::residencyP95Ms() const {
    return m_residencyUs.percentile(0.95) / 1000.0;
}

double MiniAudioSink::latencyP95Ms() const {
    return residencyP95Ms() +
           static_cast<double>(m_periodFrames) * 1000.0 / static_cast<double>(m_sampleRate);
}

} // namespace koalaamp::interop::vlc
