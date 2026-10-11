// SPDX-License-Identifier: MIT
// playback — the app-level playback service (FE-4): file → queue → libvlc
// decoded-PCM tap → SPSC ring → miniaudio sink, with transport, volume, seek,
// queue and end-of-track advance.
//
// REUSE: the engine chain is FE-2's proven `src/interop/vlc/`
// {VlcPcmTap,SpscRing,MiniAudioSink} (docs/spikes/libvlc-callbacks.md). This
// core wraps it; it does not re-derive the callback path, and it never enables
// libvlc's native equalizer (the spec §3.1 EQ rule — native EQ runs upstream of
// the callback and would double-EQ our own biquads).
//
// Qt-FREE by contract (docs/spec.md §2.1): neither this header nor its
// implementation may include a Qt header. A test greps src/playback/ to keep it
// that way; src/ui adapts IPlaybackObserver to Qt signals in SE-2.
//
// THREADING CONTRACT (load-bearing)
//   * The public surface is callable from any thread. Control calls
//     (play/pause/stop/seek/volume/queue/repeat/shuffle) are marshalled to the
//     core's own dispatch thread and executed there; state reads
//     (state()/positionMs()/volume100()/queueSize()/…) are lock-free.
//   * libvlc delivers player events (EndReached, EncounteredError, …) on its
//     own threads. The core's listener only posts into a lock-free mailbox —
//     it makes no engine call and no observer callout there.
//   * IPlaybackObserver callbacks are ALWAYS invoked on the dispatch thread,
//     never on a libvlc thread and never while a core lock is held, so an
//     observer may call back into the core.
//   * IAudioEngine::setPcmCallback is the one deliberate exception: it is the
//     DSP insertion seam and runs on libvlc's audio-output thread. It must not
//     allocate, lock, or touch the GUI (docs/spec.md §2.1).

#pragma once

#include "playback/IAudioEngine.hpp"
#include "playback/IPlaybackObserver.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace koalaamp::playback {

struct PlaybackConfig {
    /// Ring capacity is the DSP-mode latency knob and the only one libvlc
    /// offers (docs/spec.md §3.1, spike §4): VLC fills whatever ring it is
    /// given, so the ring *is* the buffer. Powers of two only, hence 200 ms
    /// (compact) → 341 ms / 700 ms (relaxed) → 1365 ms actual capacity.
    enum class RingRegime {
        Compact, ///< 200 ms request — default; 0 underruns, rare boundary drops
        Relaxed, ///< 700 ms request — exceeds libvlc's decode-ahead: 0 drops
    };
    enum class Output {
        Device, ///< miniaudio picks ALSA/Pulse/WASAPI/CoreAudio
        Null,   ///< miniaudio null backend: full chain, no device (headless/CI)
    };

    RingRegime ring{RingRegime::Compact};
    Output output{Output::Device};
    unsigned periodFrames{1024};
    /// Prefetch lead before the end of a track (docs/spec.md §3.2: 10 s).
    /// Gapless scaffolding only — the seam is real, the sample-exact boundary
    /// is AU-2.
    std::int64_t prefetchLeadMs{10000};
};

/// Snapshot of the engine chain's state (tests + future diagnostics UI).
struct PlaybackMetrics {
    std::uint64_t framesDecoded{0};  ///< tap play-callback frames (PCM flowed)
    std::uint64_t framesRendered{0}; ///< sink device-callback frames
    std::uint64_t underruns{0};      ///< sink callbacks not fully served
    std::uint64_t framesDropped{0};  ///< tap overruns (ring full)
    std::uint64_t eventsDropped{0};  ///< libvlc events lost to a full mailbox
    std::uint64_t ringFrames{0};     ///< ring capacity in frames
    double pcmRms{0.0};              ///< tap RMS of the float PCM (post-gain)
    std::string outputBackend;       ///< miniaudio backend name ("Null"/"ALSA"/…), "" if none
    bool nativeEqEnabled{false};     ///< must stay false on this path (spec §3.1)
    PlaybackState state{PlaybackState::Stopped};
    std::int64_t positionMs{0};
    std::int64_t lengthMs{0};
    int volume100{100};
    std::size_t queueSize{0};
};

class PlaybackCore final : public IAudioEngine {
public:
    explicit PlaybackCore(PlaybackConfig config = {});
    ~PlaybackCore() override;
    PlaybackCore(const PlaybackCore&) = delete;
    PlaybackCore& operator=(const PlaybackCore&) = delete;

    // --- IAudioEngine seam --------------------------------------------------
    /// DSP insertion seam: runs on libvlc's audio-output thread with the
    /// post-gain, pre-ring float block. Set before the first play().
    void setPcmCallback(PcmCb cb) override;
    /// Play `media` now: it becomes the current track and is appended to the
    /// session list (the repeat-all/shuffle fallback order).
    void play(const MediaRef& media) override;
    void pause() override;
    void stop() override;
    void seekMs(std::int64_t ms) override;
    /// 0.0 .. 1.0; mapped onto the 0-100 perceptual control.
    void setVolume(float volume) override;
    bool isPlaying() const override;

    // --- transport ----------------------------------------------------------
    void resume();
    void togglePause(); ///< Winamp pause button semantics
    void next();        ///< queue front, else session-list/repeat/shuffle
    void previous();    ///< restart if >3 s in, else the previous session entry

    // --- state --------------------------------------------------------------
    PlaybackState state() const { return m_state.load(); }
    std::int64_t positionMs() const { return m_positionMs.load(); }
    std::int64_t lengthMs() const { return m_lengthMs.load(); }
    std::optional<MediaRef> currentMedia() const;

    // --- volume (0-100 perceptual, docs/spec.md §3.3) -----------------------
    int volume100() const { return m_volume100.load(); }
    void setVolume100(int volume);

    // --- queue (deque semantics: consumed before the session-list refill) ---
    void enqueue(MediaRef media);
    void clearQueue();
    std::size_t queueSize() const { return m_queueSize.load(); }
    /// Front of the queue, else the repeat/shuffle fallback candidate.
    std::optional<MediaRef> peekNext() const;

    // --- repeat / shuffle ---------------------------------------------------
    void setRepeat(RepeatMode mode);
    RepeatMode repeat() const { return static_cast<RepeatMode>(m_repeat.load()); }
    void setShuffle(bool on);
    bool shuffle() const { return m_shuffle.load(); }

    // --- gapless scaffolding (mechanism; sample-exact concat is AU-2) -------
    /// Invoked on the dispatch thread once per track, when the position comes
    /// within `prefetchLeadMs` of the end and a next media exists. AU-2 attaches
    /// the real second pipeline (double buffer) here.
    using PrefetchHook = std::function<void(const MediaRef& next)>;
    void setPrefetchHook(PrefetchHook hook);
    bool prefetchArmed() const { return m_prefetchArmed.load(); }

    // --- observers ----------------------------------------------------------
    void addObserver(IPlaybackObserver* observer);
    void removeObserver(IPlaybackObserver* observer);

    // --- introspection ------------------------------------------------------
    PlaybackMetrics metrics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    std::atomic<PlaybackState> m_state{PlaybackState::Stopped};
    std::atomic<std::int64_t> m_positionMs{0};
    std::atomic<std::int64_t> m_lengthMs{0};
    std::atomic<int> m_volume100{100};
    std::atomic<std::size_t> m_queueSize{0};
    std::atomic<int> m_repeat{static_cast<int>(RepeatMode::Off)};
    std::atomic<bool> m_shuffle{false};
    std::atomic<bool> m_prefetchArmed{false};
};

} // namespace koalaamp::playback
