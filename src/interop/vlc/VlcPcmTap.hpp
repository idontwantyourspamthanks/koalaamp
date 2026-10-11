// SPDX-License-Identifier: MIT
// interop/vlc — libvlc decoded-PCM tap (raw C API, docs/spikes/fe0-deps.md).
//
// This is the FE-2 spike implementation of the DSP-mode source (docs/spec.md
// §3.1): libvlc decodes, its vmem/amem audio output hands us interleaved float
// PCM through libvlc_audio_set_callbacks(), and we push it into a lock-free
// ring for a miniaudio sink. No native EQ is ever used here — libvlc's
// user-filter chain (the `equalizer` module installed by
// libvlc_media_player_set_equalizer) runs *upstream* of the callback, so
// enabling it would double-EQ our biquads.

#pragma once

#include "interop/vlc/Metrics.hpp"
#include "interop/vlc/SpscRing.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <vlc/vlc.h>

namespace koalaamp::interop::vlc {

/// Format the tap requests from libvlc (fixed: FL32, 48 kHz, stereo). Set in
/// the format-setup callback on the audio-output thread, hence atomics.
struct TapFormat {
    std::atomic<unsigned> sampleRate{0};
    std::atomic<unsigned> channels{0};
};

/// Owns a libvlc instance + media player configured for the callback path.
/// Thread-safety: the public surface is called from the controlling (test /
/// GUI) thread; only the static callbacks touch state from libvlc's threads.
class VlcPcmTap {
public:
    struct Stats {
        std::atomic<std::uint64_t> callbacks{0};
        std::atomic<std::uint64_t> framesIn{0};
        std::atomic<std::uint64_t> framesDropped{0}; // ring full (overrun)
        std::atomic<std::uint64_t> flushes{0};
        std::atomic<std::uint64_t> drains{0};
        std::atomic<std::uint64_t> pauses{0};
        std::atomic<std::uint64_t> resumes{0};
        std::atomic<std::int64_t> lastCallbackNs{0};  // steady clock, ns
        std::atomic<std::int64_t> firstCallbackNs{0}; // first after arm()
        std::atomic<std::int64_t> lastPtsUs{0};       // libvlc pts, µs
    };

    explicit VlcPcmTap(std::size_t ringFloats = 8192);
    ~VlcPcmTap();
    VlcPcmTap(const VlcPcmTap&) = delete;
    VlcPcmTap& operator=(const VlcPcmTap&) = delete;

    /// Create the instance/player on first call, then set `path` as media.
    bool open(const std::string& path);

    /// Replace the current media (seam measurement). Keeps callbacks/format.
    bool setMedia(const std::string& path);

    void play();
    void pause();
    void stop();
    void seekMs(std::int64_t ms);
    bool isPlaying() const;
    std::int64_t timeMs() const; // -1 before playback starts

    // --- native EQ control surface (spec §3.1) ------------------------------
    /// Install libvlc's native equalizer with all 10 bands at `db`. The module
    /// joins the user-filter chain upstream of our callback — the spike's
    /// rule demonstration.
    void setEqualizerGainDb(double db);
    /// Remove libvlc's native equalizer (set_equalizer(NULL)): the user-filter
    /// chain is empty again and the callback returns to baseline PCM.
    void clearEqualizer();
    /// Whether libvlc currently has a native equalizer attached.
    bool hasNativeEqualizer() const;

    // --- measurement --------------------------------------------------------
    /// Pre-size the callback-interval recorder (call before play()).
    void enableTiming(std::size_t capacity = 1u << 20);
    /// Arm firstCallbackNs to capture the next play callback's timestamp.
    void armFirstCallback();

    /// Exponential mean-square of the PCM seen in the callback; RMS = sqrt.
    double rmsLevel() const;
    double callbackIntervalP50ms() const;
    double callbackIntervalP95ms() const;

    FloatRing& ring() noexcept { return m_ring; }
    const FloatRing& ring() const noexcept { return m_ring; }
    const TapFormat& format() const noexcept { return m_format; }
    Stats& stats() noexcept { return m_stats; }
    const Stats& stats() const noexcept { return m_stats; }
    libvlc_media_player_t* player() const noexcept { return m_mp; }

private:
    static void cbPlay(void* data, const void* samples, unsigned count, std::int64_t pts);
    static void cbPause(void* data, std::int64_t pts);
    static void cbResume(void* data, std::int64_t pts);
    static void cbFlush(void* data, std::int64_t pts);
    static void cbDrain(void* data);
    static int cbSetup(void** data, char* format, unsigned* rate, unsigned* channels);

    libvlc_instance_t* m_instance{nullptr};
    libvlc_media_player_t* m_mp{nullptr};
    libvlc_media_t* m_media{nullptr};

    FloatRing m_ring;
    TapFormat m_format;
    Stats m_stats;
    SampleRecorder m_cbIntervalsUs;
    std::vector<float> m_scratch; // s16→f32 conversion buffer (producer thread only)
    std::atomic<double> m_msEma{0.0};
    std::atomic<bool> m_armed{false};
    std::atomic<bool> m_nativeEq{false}; // libvlc 3.x has no EQ getter
};

} // namespace koalaamp::interop::vlc
