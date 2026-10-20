// SPDX-License-Identifier: MIT
// playback — the app-level playback service (FE-4): file → queue → libvlc
// decoded-PCM tap → SPSC ring → miniaudio sink, with transport, volume, seek,
// queue and end-of-track advance.
//
// AU-2 adds the two things that make that chain's track boundaries real: a
// sample-exact gapless handover on the DSP path (one decoder, one ring, no stop
// and no discard at the natural end — `PlaybackConfig::gapless`) and the
// per-track prepare seam the ReplayGain stage is populated through
// (`setTrackPrepareHook`). The boundary is only ever taken after an observed
// natural end; everything else stays a deliberate discontinuity.
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
//     observer may call back into the core. An observer MUST outlive the core
//     (or be removed before it is destroyed): the dispatch thread keeps
//     calling out until the destructor joins it, so an observer declared after
//     the core — and therefore destroyed first — can still be called.
//   * IAudioEngine::setPcmCallback is the one deliberate exception: it is the
//     DSP insertion seam and runs on libvlc's audio-output thread, from the
//     first decoded block until the core's destructor stops the tap. It must not
//     allocate, lock, or touch the GUI (docs/spec.md §2.1), and whatever the
//     callback captures MUST outlive the core — declare the core last (so it is
//     destroyed first) relative to the transform it drives. The destructor
//     closes the seam and waits (bounded) for an in-flight callback to return
//     before the tap is released, so releasing the core while a track plays is
//     safe.

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
#include <vector>

namespace koalaamp::playback {

struct PlaybackConfig {
    /// Ring capacity is the DSP-mode latency knob and the only one libvlc
    /// offers (docs/spec.md §3.1, spike §4): VLC fills whatever ring it is
    /// given, so the ring *is* the buffer. Powers of two only, hence 200 ms
    /// (compact) → 341 ms / 700 ms (relaxed) → 1365 ms actual capacity.
    enum class RingRegime {
        Compact, ///< 200 ms request → 341 ms — NOT gapless-safe: libvlc's
                 ///< decode-ahead burst (~1.15 s, measured) does not fit, so
                 ///< every play/seek/boundary drops ~0.9 s of audio (AU-2)
        Relaxed, ///< 700 ms request → 1365 ms — exceeds libvlc's decode-ahead:
                 ///< 0 drops; the gapless boundary needs this (AU-2)
    };
    enum class Output {
        Device, ///< miniaudio picks ALSA/Pulse/WASAPI/CoreAudio
        Null,   ///< miniaudio null backend: full chain, no device (headless/CI)
    };

    /// Relaxed by default: AU-2 measured that the compact ring drops ~0.9 s of
    /// audio at every play, seek *and* gapless boundary (libvlc hands over its
    /// ~1.15 s decode-ahead in one burst; a ring smaller than that can only
    /// discard the excess). FE-4 pinned compact before gapless existed and left
    /// the decision here (its finding 1). Compact stays selectable for a lower
    /// steady-state latency; it is simply not gapless-capable.
    RingRegime ring{RingRegime::Relaxed};
    Output output{Output::Device};
    unsigned periodFrames{1024};
    /// Prefetch lead before the end of a track (docs/spec.md §3.2: 10 s). The
    /// next media is resolved here; AU-2's gapless handover uses the resolved
    /// next and the ReplayGain prepare hook.
    std::int64_t prefetchLeadMs{10000};
    /// Gapless boundary (AU-2, DSP path). At the natural end the core hands the
    /// *same* decoder the next media instead of stopping it, and swaps while the
    /// outgoing track's ring tail has drained: the tail covers the incoming
    /// track's media activation while the freed space absorbs its decode burst.
    /// `gaplessTailMs` is the *cap* on that tail — the actual trigger is
    /// `min(gaplessTailMs, ring capacity − learned decode-ahead − 50 ms)`, so a
    /// slower-decoding source (whose burst is deeper) automatically keeps less
    /// tail rather than dropping the incoming burst. false = the FE-4 behaviour
    /// (stop + discard at the boundary: a ~1 s loss, kept as an escape hatch).
    bool gapless{true};
    double gaplessTailMs{150.0};
};

/// Snapshot of the engine chain's state (tests + future diagnostics UI).
struct PlaybackMetrics {
    std::uint64_t framesDecoded{0};  ///< tap play-callback frames (PCM flowed)
    std::uint64_t framesRendered{0}; ///< sink device-callback frames
    std::uint64_t underruns{0};      ///< sink callbacks not fully served
    std::uint64_t framesDropped{0};  ///< tap overruns (ring full)
    /// Audio thrown away by an explicit discontinuity (AU-2): the ring drained
    /// by a seek/stop/track switch. 0 across a gapless boundary — that is the
    /// measurement that says the boundary lost nothing.
    std::uint64_t framesDiscarded{0};
    /// Frames the sink served as silence (a starved ring): the audible gap.
    std::uint64_t silenceFrames{0};
    std::uint64_t gaplessHandovers{0}; ///< boundaries taken on the gapless path
    /// Ring tail (in ms) that was still unrendered when the last gapless swap
    /// happened — the audio covering the incoming media's activation. The
    /// boundary's whole budget is `tail + free ring vs the incoming burst`.
    double lastBoundaryTailMs{0.0};
    /// Deepest ring occupancy seen = libvlc's decode-ahead for the media played
    /// so far, in frames. The gapless budget is `ringFrames - this`.
    std::uint64_t decodeAheadFrames{0};
    std::uint64_t eventsDropped{0}; ///< libvlc events lost to a full mailbox
    std::uint64_t ringFrames{0};    ///< ring capacity in frames
    double pcmRms{0.0};             ///< tap RMS of the float PCM (post-gain)
    std::string outputBackend;      ///< miniaudio backend name ("Null"/"ALSA"/…), "" if none
    bool nativeEqEnabled{false};    ///< must stay false on this path (spec §3.1)
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
    /// Start a whole list atomically: clears the old queue/history, queues every
    /// medium after `startIndex`, and starts that index. Use this instead of
    /// clearQueue()+play()+enqueue() so an old track ending cannot consume the
    /// new list's first queued row.
    void playList(std::vector<MediaRef> list, std::size_t startIndex);
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
    /// Insert at the *front* of the queue (the remote-control "PlayNext"
    /// semantics: next up, without touching the current track).
    void enqueueNext(MediaRef media);
    void clearQueue();
    std::size_t queueSize() const { return m_queueSize.load(); }
    /// Front of the queue, else the repeat/shuffle fallback candidate.
    std::optional<MediaRef> peekNext() const;

    // --- repeat / shuffle ---------------------------------------------------
    void setRepeat(RepeatMode mode);
    RepeatMode repeat() const { return static_cast<RepeatMode>(m_repeat.load()); }
    void setShuffle(bool on);
    bool shuffle() const { return m_shuffle.load(); }

    // --- gapless boundary + per-track prepare (AU-2) ------------------------
    /// Invoked on the dispatch thread once per track, when the position comes
    /// within `prefetchLeadMs` of the end and a next media exists: the core's
    /// scheduler prefetch (docs/spec.md §3.2). Resolution only — the boundary
    /// itself is the core's job (see `gapless` in PlaybackConfig).
    using PrefetchHook = std::function<void(const MediaRef& next)>;
    void setPrefetchHook(PrefetchHook hook);
    bool prefetchArmed() const { return m_prefetchArmed.load(); }

    /// Invoked on the dispatch thread immediately *before* a track's first
    /// samples can be decoded, with the media about to start. This is the
    /// ReplayGain activation seam (AU-2): the wiring layer reads the track's
    /// tags (`audio::readReplayGainTags`) and calls
    /// `DspPipeline::setReplayGain` here. It fires on both paths — a full
    /// activation and a gapless handover — and always before `play()`, so the
    /// transform latches the new gain on the track's first block.
    using TrackPrepareHook = std::function<void(const MediaRef& media)>;
    void setTrackPrepareHook(TrackPrepareHook hook);

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
