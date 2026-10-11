// SPDX-License-Identifier: MIT
// interop/vlc — miniaudio playback sink driven by the libvlc callback ring.
//
// FE-2 spike: pull interleaved float PCM from a FloatRing on miniaudio's
// device thread and render it through WASAPI/CoreAudio/ALSA/Pulse. Records the
// backlog (residency) per device callback and counts underruns so the latency
// and xrun acceptance criteria can be measured.

#pragma once

#include "interop/vlc/Metrics.hpp"
#include "interop/vlc/SpscRing.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

struct ma_device; // miniaudio (third_party/include/miniaudio.h)

namespace koalaamp::interop::vlc {

enum class SinkBackend { Auto, Alsa, Pulse };

struct SinkConfig {
    unsigned sampleRate{48000};
    unsigned channels{2};
    unsigned periodFrames{1024};
    SinkBackend backend{SinkBackend::Auto};
};

struct SinkStats {
    std::atomic<std::uint64_t> deviceCallbacks{0};
    std::atomic<std::uint64_t> underruns{0}; // callback not fully served (starved)
    std::atomic<std::uint64_t> framesRendered{0};
    std::atomic<std::uint64_t> silenceFrames{0};
};

class MiniAudioSink {
public:
    explicit MiniAudioSink(FloatRing& ring);
    ~MiniAudioSink();
    MiniAudioSink(const MiniAudioSink&) = delete;
    MiniAudioSink& operator=(const MiniAudioSink&) = delete;

    bool start(const SinkConfig& config);
    void stop();

    /// Backend actually chosen by miniaudio ("alsa", "pulseaudio", ...).
    const std::string& backendName() const noexcept { return m_backend; }
    /// Device period miniaudio settled on (frames).
    unsigned periodFrames() const noexcept { return m_periodFrames; }
    const SinkStats& stats() const noexcept { return m_stats; }

    /// Backlog (ring occupancy, in frames) sampled after each device callback.
    double backlogP95Ms() const;
    double backlogMaxMs() const;
    unsigned backlogP95Frames() const;

    /// Enqueue→device-pull residency (the ticket's callback-timestamp delta).
    double residencyP95Ms() const;
    /// End-to-end estimate: residency p95 plus one device period.
    double latencyP95Ms() const;

private:
    static void dataCallback(ma_device* device, void* output, const void* input,
                             unsigned frameCount);

    struct Impl;
    Impl* m_impl{nullptr};
    FloatRing& m_ring;
    SinkStats m_stats;
    SampleRecorder m_backlog;
    SampleRecorder m_residencyUs;
    std::array<std::int64_t, 128> m_residencyScratch{};
    std::string m_backend;
    unsigned m_channels{2};
    unsigned m_sampleRate{48000};
    unsigned m_periodFrames{0};
};

} // namespace koalaamp::interop::vlc
