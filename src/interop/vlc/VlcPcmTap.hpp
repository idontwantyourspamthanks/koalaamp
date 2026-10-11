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
#include <functional>
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

/// Media-player events the tap republishes (FE-4). `value` carries the payload
/// where one exists (LengthChanged → new length in ms, else 0).
enum class TapEvent {
    EndReached,
    EncounteredError,
    Playing,
    Paused,
    Stopped,
    LengthChanged,
};

/// Invoked on a libvlc thread, from libvlc's event loop (FE-4). The handler
/// must only marshal — no engine call, no blocking: calling back into libvlc
/// from here can deadlock. Set it before play().
///
/// `value` carries the payload where one exists (LengthChanged → new length in
/// ms, else 0). `generation` is the media generation at *emit* time
/// (generation()): events for a media that has since been replaced can be
/// recognised and dropped, which matters because libvlc can deliver a late
/// EndReached for the media a consumer has already advanced past. Note that a
/// real EndReached legitimately arrives with positionMs well short of
/// lengthMs (VLC's callback-path clock stalls ~1 s before the end), so a stale
/// event must NOT be detected by comparing the two.
using TapEventListener =
    std::function<void(TapEvent event, std::int64_t value, std::uint64_t generation)>;

/// Decoded-PCM observer: interleaved float, sample rate, channels. Invoked on
/// libvlc's audio-output thread — the DSP insertion seam (docs/spec.md §3.1).
/// MUST NOT allocate, lock, or touch the GUI (docs/spec.md §2.1). Set it
/// before play().
using TapPcmObserver =
    std::function<void(const float* interleaved, std::size_t frames, unsigned rate,
                       unsigned channels)>;

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

    /// Same, for a network/stream locator (MRL) — libvlc_media_new_location.
    bool openLocation(const std::string& url);

    /// Replace the current media (seam measurement). Keeps callbacks/format.
    bool setMedia(const std::string& path);

    /// Replace the current media with a locator (MRL): http(s)://, file://, ...
    bool setMediaLocation(const std::string& url);

    void play();
    void pause();
    void resume();
    void stop();
    void seekMs(std::int64_t ms);
    bool isPlaying() const;
    std::int64_t timeMs() const;   // -1 before playback starts
    std::int64_t lengthMs() const; // -1 until the input reports a length

    // --- FE-4 seams (set before play()) -------------------------------------
    /// Media-player events, delivered on a libvlc thread (see TapEventListener).
    void setEventListener(TapEventListener listener);
    /// Counts how many times a media has been set on this player. An event
    /// tagged with a generation other than the current one is stale — it was
    /// emitted for a media that has been replaced since.
    std::uint64_t generation() const { return m_generation.load(std::memory_order_acquire); }
    /// Decoded PCM, delivered on libvlc's audio-output thread (DSP seam).
    void setPcmObserver(TapPcmObserver observer);
    /// Master gain applied to the float PCM before the ring/observer (the
    /// playback core's volume stage). One multiply per sample, RT-safe.
    void setOutputGain(float linear);
    float outputGain() const { return m_gain.load(std::memory_order_relaxed); }

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
    bool ensureInstance();
    bool attachEvents();
    bool setMediaImpl(const std::string& source, bool location);

    static void cbPlay(void* data, const void* samples, unsigned count, std::int64_t pts);
    static void cbPause(void* data, std::int64_t pts);
    static void cbResume(void* data, std::int64_t pts);
    static void cbFlush(void* data, std::int64_t pts);
    static void cbDrain(void* data);
    static int cbSetup(void** data, char* format, unsigned* rate, unsigned* channels);
    static void cbEvent(const libvlc_event_t* event, void* data);

    libvlc_instance_t* m_instance{nullptr};
    libvlc_media_player_t* m_mp{nullptr};
    libvlc_media_t* m_media{nullptr};
    libvlc_event_manager_t* m_eventManager{nullptr};

    FloatRing m_ring;
    TapFormat m_format;
    Stats m_stats;
    SampleRecorder m_cbIntervalsUs;
    std::vector<float> m_scratch; // s16→f32 conversion buffer (producer thread only)
    std::atomic<double> m_msEma{0.0};
    std::atomic<bool> m_armed{false};
    std::atomic<bool> m_nativeEq{false}; // libvlc 3.x has no EQ getter
    std::atomic<float> m_gain{1.0f};     // FE-4 master gain (volume stage)
    std::atomic<std::uint64_t> m_generation{0}; // bumped per media set (stale-event tag)
    TapEventListener m_listener;         // set before play(), read on VLC threads
    TapPcmObserver m_pcmObserver;        // ditto
};

} // namespace koalaamp::interop::vlc
