// SPDX-License-Identifier: MIT
// playback — FE-4 PlaybackCore implementation.
//
// Wraps the FE-2 engine chain (docs/spikes/libvlc-callbacks.md): the libvlc
// decoded-PCM tap, its lock-free ring and the miniaudio sink are used exactly
// as proven there. See PlaybackCore.hpp for the threading contract.

#include "playback/PlaybackCore.hpp"

#include "interop/vlc/MiniAudioSink.hpp"
#include "interop/vlc/VlcPcmTap.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace koalaamp::playback {
namespace {

using interop::vlc::MiniAudioSink;
using interop::vlc::SinkBackend;
using interop::vlc::SinkConfig;
using interop::vlc::TapEvent;
using interop::vlc::VlcPcmTap;

constexpr unsigned kSampleRate = 48000; // the tap's fixed output format
constexpr unsigned kChannels = 2;
/// Session-list cap: keeps repeat-all/previous bounded in an endless session.
constexpr std::size_t kMaxSessionList = 512;
/// `previous()` restarts the current track instead of stepping back beyond this.
constexpr std::int64_t kPrevRestartMs = 3000;
/// AU-2: never let the boundary tail fall below this. The tail is what covers
/// the incoming media's activation; without it the boundary is a gap, so the
/// floor is preferred over an exactly-fitting burst. Measured on the reference
/// machine's relaxed ring: 50 ms keeps both the burst and the activation inside
/// the ring for the codecs probed (WAV ~1.15 s, FLAC ~1.27 s decode-ahead).
constexpr unsigned kHandoverFloorMs = 50;
constexpr auto kTick = std::chrono::milliseconds(10);

std::size_t ringFloats(PlaybackConfig::RingRegime regime) {
    const double ms = regime == PlaybackConfig::RingRegime::Relaxed ? 700.0 : 200.0;
    return static_cast<std::size_t>(static_cast<double>(kSampleRate) * kChannels * ms / 1000.0);
}

/// 0-100 perceptual volume → linear amplitude. Squared-law curve (half slider
/// ≈ -12 dB) — monotone and testable; AU-1 owns the final curve (spec §3.3).
float gainFor(int volume100) {
    const float n = static_cast<float>(std::clamp(volume100, 0, 100)) / 100.0f;
    return n * n;
}

/// libvlc thread(s) → dispatch thread. Fixed capacity, no allocation, no
/// blocking: a full mailbox drops (events are a handful per track; the cap is a
/// safety net against a runaway producer, counted in `dropped()`).
class EventMailbox {
public:
    struct Item {
        TapEvent event;
        std::int64_t value;
        std::uint64_t generation; ///< media generation at emit time (tap)
    };

    bool push(TapEvent event, std::int64_t value, std::uint64_t generation) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const std::size_t next = (m_head + 1) % m_buf.size();
        if (next == m_tail) {
            ++m_dropped;
            return false;
        }
        m_buf[m_head] = Item{event, value, generation};
        m_head = next;
        return true;
    }

    bool pop(Item& out) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_tail == m_head) {
            return false;
        }
        out = m_buf[m_tail];
        m_tail = (m_tail + 1) % m_buf.size();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_tail == m_head;
    }

    std::uint64_t dropped() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_dropped;
    }

private:
    std::array<Item, 64> m_buf{};
    mutable std::mutex m_mutex;
    std::size_t m_head{0};
    std::size_t m_tail{0};
    std::uint64_t m_dropped{0};
};

/// Control command. Executed on the dispatch thread; larger than a TapEvent
/// because control-path allocation/copy is not on any audio path.
struct Command {
    enum class Type {
        Play,
        Pause,
        Resume,
        Stop,
        Seek,
        Next,
        Prev,
        SetVolume,
        SetPrefetchHook,
        SetTrackPrepareHook,
    };
    explicit Command(Type commandType) : type(commandType) {}
    Type type;
    MediaRef media;
    std::int64_t i64{0};
    int i{0};
    PlaybackCore::PrefetchHook hook;
    PlaybackCore::TrackPrepareHook prepareHook;
};

} // namespace

struct PlaybackCore::Impl {
    Impl(PlaybackCore& owner, PlaybackConfig cfg) : self(owner), config(cfg) {}
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    enum class HistAction { Append, Keep, At };

    PlaybackCore& self;
    PlaybackConfig config;

    // --- control path -------------------------------------------------------
    std::mutex cmdMutex;
    std::condition_variable wakeup;
    std::deque<Command> commands;
    bool shutdown{false};
    std::thread dispatch;

    // --- engine (created on first play, torn down in ~PlaybackCore) ---------
    std::unique_ptr<VlcPcmTap> tap;
    std::unique_ptr<MiniAudioSink> sink;
    bool enginePrimed{false};    // tap has had its first (open) media
    bool enginePlaying{false};   // dispatch thread only
    PcmCb pcmCb;                 // DSP seam; applied to the tap at creation
    // PCM-seam quiesce handshake (AU-2). The tap's callback runs on libvlc's
    // audio-output thread and is handed the tap's own scratch buffer, so a
    // callback that is in flight while the tap is torn down would touch freed
    // memory. Teardown closes the gate first and waits for the in-flight count
    // to drain. Two atomics, no lock — the audio thread never blocks.
    std::atomic<int> pcmInFlight{0};
    std::atomic<bool> pcmBlocked{false};
    /// Published after the engine members are fully constructed (acquire/release
    /// pair for metrics() reads from the owner thread).
    std::atomic<bool> engineReady{false};

    // --- state machine ------------------------------------------------------
    mutable std::mutex stateMutex;
    std::optional<MediaRef> current;
    std::deque<MediaRef> queue;
    std::vector<MediaRef> session; // play order: repeat-all/previous fallback
    std::size_t sessionIndex{0};
    std::atomic<bool> trackPrefetched{false};
    bool ended{false};           // dispatch thread: EndReached seen for this media
    bool handoverPending{false}; // dispatch thread: the boundary is armed (AU-2)
    std::optional<MediaRef> pendingNext; // dispatch thread: parked at arm, published at swap
    PrefetchHook prefetchHook;   // dispatch thread only (set via command)
    TrackPrepareHook prepareHook;
    std::uint64_t handovers{0};  // gapless boundaries taken (dispatch thread)
    std::uint64_t peakFillFrames{0}; // deepest ring fill = libvlc's decode-ahead (AU-2)
    double lastBoundaryTailMs{0.0};  // published per boundary (tick thread only)
    mutable std::uint32_t rngState{0x2545f491u};

    // --- observers ----------------------------------------------------------
    std::mutex observerMutex;
    std::vector<IPlaybackObserver*> observers;

    EventMailbox mailbox;

    // notification bookkeeping (dispatch thread only)
    std::int64_t lastPos{-1};
    std::int64_t lastLen{-1};
    int lastVolume{100};

    // =====================  control-path plumbing  =========================

    void request(Command command) {
        {
            std::lock_guard<std::mutex> lock(cmdMutex);
            if (shutdown) {
                return;
            }
            commands.push_back(std::move(command));
        }
        wakeup.notify_one();
    }

    template <typename Fn> void forEachObserver(Fn&& fn) {
        std::array<IPlaybackObserver*, 64> snapshot{};
        std::size_t n = 0;
        {
            std::lock_guard<std::mutex> lock(observerMutex);
            n = std::min(observers.size(), snapshot.size());
            std::copy_n(observers.begin(), n, snapshot.begin());
        }
        for (std::size_t i = 0; i < n; ++i) {
            fn(*snapshot[i]);
        }
    }

    void setState(PlaybackState state) {
        if (self.m_state.exchange(state) == state) {
            return; // observers see changes only
        }
        forEachObserver([&](IPlaybackObserver& obs) { obs.onStateChanged(state); });
    }

    // ==========================  dispatch loop  ============================

    void run() {
        std::unique_lock<std::mutex> lock(cmdMutex);
        while (!shutdown) {
            wakeup.wait_for(lock, kTick, [this] {
                return shutdown || !commands.empty() || !mailbox.empty();
            });
            if (shutdown) {
                break;
            }
            std::deque<Command> pending;
            pending.swap(commands);
            lock.unlock();

            EventMailbox::Item item{};
            while (mailbox.pop(item)) {
                handleEvent(item);
            }
            for (const Command& command : pending) {
                handleCommand(command);
            }
            tick();

            lock.lock();
        }
    }

    void handleCommand(const Command& command) {
        switch (command.type) {
        case Command::Type::Play:
            activate(command.media, HistAction::Append, 0);
            break;
        case Command::Type::Pause:
            if (tap && self.m_state.load() == PlaybackState::Playing) {
                tap->pause();
                setState(PlaybackState::Paused);
            }
            break;
        case Command::Type::Resume:
            if (tap && self.m_state.load() == PlaybackState::Paused) {
                tap->resume();
                setState(PlaybackState::Playing);
            }
            break;
        case Command::Type::Stop:
            stopInternal();
            break;
        case Command::Type::Seek:
            if (tap && currentLocked().has_value()) {
                // AU-2: the ring is cleared here, not in the tap's flush
                // callback: a seek *is* the discontinuity (the pre-seek frames
                // are invalid), whereas the flush that arrives at end-of-input
                // must leave the last ~1 s of the track intact for gapless.
                tap->seekMs(command.i64);
                tap->ring().discardAll();
                handoverPending = false;
                ended = false;
                publishPosition(std::max<std::int64_t>(0, command.i64), self.m_lengthMs.load());
            }
            break;
        case Command::Type::Next:
            advance(false);
            break;
        case Command::Type::Prev:
            previous();
            break;
        case Command::Type::SetVolume:
            if (tap) {
                tap->setOutputGain(gainFor(command.i));
            }
            if (lastVolume != command.i) {
                lastVolume = command.i;
                forEachObserver([&](IPlaybackObserver& obs) { obs.onVolumeChanged(command.i); });
            }
            break;
        case Command::Type::SetPrefetchHook:
            prefetchHook = command.hook;
            break;
        case Command::Type::SetTrackPrepareHook:
            prepareHook = command.prepareHook;
            break;
        }
    }

    void handleEvent(const EventMailbox::Item& item) {
        // A late EndReached/error from a media we have already replaced must
        // not touch the state machine (it would skip a track). The tap tags
        // every event with the media generation at emit time, so this is an
        // exact test — unlike comparing positionMs to lengthMs, which a real
        // end does not satisfy here (VLC's callback-path clock stalls ~1 s
        // short of the end).
        const bool stale = tap && item.generation != tap->generation();
        switch (item.event) {
        case TapEvent::EndReached:
            if (!stale) {
                handleEndReached();
            }
            break;
        case TapEvent::EncounteredError:
            if (stale) {
                break;
            }
            forEachObserver([&](IPlaybackObserver& obs) {
                obs.onError("libvlc reported an error for the current media");
            });
            advance(false);
            break;
        case TapEvent::LengthChanged:
            if (stale) {
                break;
            }
            if (item.value >= 0 && lastLen != item.value) {
                lastLen = item.value;
                self.m_lengthMs.store(item.value);
                forEachObserver([&](IPlaybackObserver& obs) {
                    obs.onPositionChanged(self.m_positionMs.load(), item.value);
                });
            }
            break;
        // Playing/Paused/Stopped are informational: the command path above owns
        // the published state, so consuming these too would double-notify.
        case TapEvent::Playing:
        case TapEvent::Paused:
        case TapEvent::Stopped:
            break;
        }
    }

    void handleEndReached() {
        if (!enginePlaying || ended) {
            // Already advanced, or a stop raced the end. Note the gapless path
            // keeps `enginePlaying` true across the end (the decoder is still
            // producing the outgoing track's tail), which is exactly why the
            // de-duplication cannot key off it.
            return;
        }
        // `enginePlaying` stays true: on the gapless path the decoder is still
        // producing (the outgoing track's tail is in the ring) until the swap.
        // `ended` is what de-duplicates a repeated EndReached for the same media.
        ended = true;
        const MediaRef finished = currentLocked().value_or(MediaRef{});
        forEachObserver([&](IPlaybackObserver& obs) { obs.onEndReached(finished); });
        advance(true);
    }

    void tick() {
        if (!tap) {
            return;
        }
        // Stopped: position is 0 by definition, and libvlc's clock may still
        // report the last media's time — never publish that as a position.
        if (self.m_state.load() == PlaybackState::Stopped) {
            return;
        }
        // Learn the engine's decode-ahead depth (AU-2): the deepest fill it
        // reaches is the burst the *next* activation will hand over, and the
        // boundary rule needs that number to reserve room for it.
        peakFillFrames = std::max<std::uint64_t>(peakFillFrames,
                                                 tap->ring().fill() / kChannels);
        const std::int64_t posRaw = tap->timeMs();
        const std::int64_t lenRaw = tap->lengthMs();
        const std::int64_t pos = posRaw < 0 ? 0 : posRaw;
        const std::int64_t len = lenRaw < 0 ? 0 : lenRaw;
        if (pos != lastPos || len != lastLen) {
            lastPos = pos;
            lastLen = len;
            publishPosition(pos, len);
        }
        maybePrefetch(pos, len);
        maybeHandover();
    }

    /// AU-2 boundary trigger. The prefetch lead (10 s) resolves the next media
    /// long before this; the swap itself waits for the *ring*, and the rule is
    /// derived from the engine's own numbers rather than a constant:
    ///
    ///   free the incoming media's activation needs = its decode-ahead burst
    ///   (measured 1.15 s for WAV, 1.27 s for FLAC on this machine — libvlc
    ///   hands its whole decode-ahead over in one go as soon as a media starts),
    ///   learned as the deepest fill this engine has reached (`peakFillFrames`);
    ///   the tail we keep must fit beside it, so
    ///       tail = min(gaplessTailMs, capacity - peakFill - margin)
    ///
    /// The tail is then the audio still playing when the incoming media is
    /// activated — long enough to cover that activation, never long enough that
    /// its burst has to be dropped.
    void maybeHandover() {
        if (!handoverPending || !tap || config.gaplessTailMs <= 0.0) {
            return;
        }
        const std::uint64_t capacity = tap->ring().capacity() / kChannels;
        const std::uint64_t floorFrames = kSampleRate * kHandoverFloorMs / 1000;
        // `peakFillFrames` is initialised to the capacity ("nothing learned: no
        // spare room"), so before the first measurement the boundary keeps only
        // the floor. After that it keeps the measured slack.
        const std::uint64_t budget = capacity > peakFillFrames ? capacity - peakFillFrames : 0;
        const std::uint64_t requested =
            static_cast<std::uint64_t>(config.gaplessTailMs * static_cast<double>(kSampleRate) / 1000.0);
        const std::uint64_t tail = std::min(requested, std::max(budget, floorFrames));
        if (tap->ring().fill() / kChannels > tail) {
            return;
        }
        lastBoundaryTailMs =
            static_cast<double>(tap->ring().fill() / kChannels) * 1000.0 / kSampleRate;
        handoverPending = false;
        swapToNext();
    }

    void publishPosition(std::int64_t pos, std::int64_t len) {
        self.m_positionMs.store(pos);
        self.m_lengthMs.store(len);
        forEachObserver([&](IPlaybackObserver& obs) { obs.onPositionChanged(pos, len); });
    }

    // ====================  prefetch + the gapless boundary  ================
    // The prefetch lead resolves the next media (spec §3.2's scheduler
    // prefetch); the boundary itself is the core's — see maybeHandover().

    void maybePrefetch(std::int64_t pos, std::int64_t len) {
        if (config.prefetchLeadMs <= 0 || !prefetchHook || trackPrefetched.load() ||
            self.m_state.load() != PlaybackState::Playing || len <= 0) {
            return;
        }
        if (len - pos > config.prefetchLeadMs) {
            return;
        }
        const std::optional<MediaRef> next = peekNextInternal();
        if (!next) {
            return;
        }
        trackPrefetched.store(true);
        self.m_prefetchArmed.store(true);
        forEachObserver([&](IPlaybackObserver& obs) { obs.onPrefetch(*next); });
        prefetchHook(*next);
    }

    // =========================  state machine  =============================

    std::optional<MediaRef> currentLocked() const {
        std::lock_guard<std::mutex> lock(stateMutex);
        return current;
    }

    std::optional<MediaRef> peekNextInternal() const {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!queue.empty()) {
            return queue.front();
        }
        const auto mode = static_cast<RepeatMode>(self.m_repeat.load());
        if (mode == RepeatMode::All && !session.empty()) {
            const std::size_t idx = self.m_shuffle.load() ? peekShuffleIndexLocked()
                                                          : (sessionIndex + 1) % session.size();
            return session[idx];
        }
        return std::nullopt;
    }

    /// Requires `stateMutex` held. That lock — not a thread assumption — is
    /// what makes this safe: `peekNext()` is public and can be called from any
    /// thread, while `planAdvance` shuffles from the dispatch thread.
    /// `m_rngState` is `mutable` only because a const *peek* still advances the
    /// candidate order.
    std::size_t peekShuffleIndexLocked() const {
        if (session.size() <= 1) {
            return 0;
        }
        std::size_t idx = sessionIndex;
        for (int i = 0; i < 8 && idx == sessionIndex; ++i) {
            idx = nextRandom() % session.size();
        }
        return idx == sessionIndex ? (sessionIndex + 1) % session.size() : idx;
    }

    std::size_t nextRandom() const {
        std::uint32_t& s = rngState;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }

    struct Plan {
        bool act{false};
        bool replay{false};
        MediaRef media;
        HistAction hist{HistAction::Append};
        std::size_t at{0};
    };

    Plan planAdvance(bool autoEnded) {
        Plan plan;
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!current) {
            return plan;
        }
        const auto mode = static_cast<RepeatMode>(self.m_repeat.load());
        if (autoEnded && mode == RepeatMode::One) {
            plan.act = true;
            plan.replay = true;
            plan.media = *current;
            plan.hist = HistAction::Keep;
            return plan;
        }
        if (!queue.empty()) { // deque semantics: the queue is consumed first
            plan.act = true;
            plan.media = queue.front();
            queue.pop_front();
            self.m_queueSize.store(queue.size());
            plan.hist = HistAction::Append;
            return plan;
        }
        if (mode == RepeatMode::All && !session.empty()) {
            const std::size_t idx = self.m_shuffle.load() ? peekShuffleIndexLocked()
                                                          : (sessionIndex + 1) % session.size();
            plan.act = true;
            plan.media = session[idx];
            plan.hist = HistAction::At;
            plan.at = idx;
        }
        return plan;
    }

    void advance(bool autoEnded) {
        Plan plan = planAdvance(autoEnded);
        if (!plan.act) {
            stopInternal();
            return;
        }
        if (autoEnded && config.gapless && tap) {
            // Gapless boundary (AU-2): this is a *natural* end, so the current
            // media is done and its ring tail is the only audio still to render.
            // Do not stop/restart: arm the swap and let the tick do it when the
            // tail has drained far enough (maybeHandover). The next media was
            // resolved by the prefetch lead, so no engine-side preparation is
            // needed at the end itself.
            armHandover(plan);
            return;
        }
        activate(plan.media, plan.hist, plan.at);
    }

    /// Book the plan and arm the boundary swap. `current` is deliberately NOT
    /// switched here: the outgoing track is still the audible one until the swap
    /// (position observers keep tracking it), so the pending media is parked and
    /// published at swapToNext().
    void armHandover(const Plan& plan) {
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (plan.hist == HistAction::Append) {
                session.push_back(plan.media);
                while (session.size() > kMaxSessionList) {
                    session.erase(session.begin());
                }
                sessionIndex = session.size() - 1;
            } else if (plan.hist == HistAction::At) {
                sessionIndex = plan.at;
            }
            pendingNext = plan.media;
        }
        trackPrefetched.store(false);
        self.m_prefetchArmed.store(false);
        handoverPending = true;
    }

    /// The swap itself: one decoder keeps producing, the ring is never cleared,
    /// so the incoming track's first frame lands immediately after the outgoing
    /// track's last one. It is only ever called from maybeHandover(), i.e. after
    /// a natural EndReached and once the outgoing tail has drained — never to
    /// cut a track short.
    void swapToNext() {
        if (!enginePlaying || !pendingNext || !tap) {
            return;
        }
        const MediaRef target = *pendingNext;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            pendingNext.reset();
            current = target;
        }
        enginePlaying = false;
        ended = false;
        // Per-track transform activation (AU-2's ReplayGain seam): before
        // play(), so the DSP stage latches the incoming track's gain on its
        // first block.
        if (prepareHook) {
            prepareHook(target);
        }
        const bool streamed = target.kind == MediaRef::Kind::Stream;
        const bool ok =
            streamed ? tap->setMediaStream(target.stream)
                     : (target.kind == MediaRef::Kind::Url ? tap->setMediaLocation(target.value)
                                                           : tap->setMedia(target.value));
        if (!ok) {
            const std::string what = "libvlc could not open media: " +
                                     (streamed ? std::string("<stream>") : target.value);
            forEachObserver([&](IPlaybackObserver& obs) { obs.onError(what); });
            stopInternal();
            return;
        }
        tap->play();
        enginePlaying = true;
        ++handovers;
        forEachObserver([&](IPlaybackObserver& obs) { obs.onTrackChanged(target); });
        self.m_positionMs.store(0);
        self.m_lengthMs.store(0);
        lastPos = -1;
        lastLen = -1;
        setState(PlaybackState::Playing);
    }

    void previous() {
        if (!currentLocked() || !tap) {
            return;
        }
        // Winamp semantics: restart the track once it is a few seconds in.
        if (self.m_positionMs.load() > kPrevRestartMs) {
            tap->seekMs(0);
            tap->ring().discardAll();
            handoverPending = false;
            publishPosition(0, self.m_lengthMs.load());
            return;
        }
        std::optional<MediaRef> target;
        std::size_t idx = 0;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (session.size() > 1 && sessionIndex > 0) {
                idx = sessionIndex - 1;
                target = session[idx];
            }
        }
        if (target) {
            activate(*target, HistAction::At, idx);
        } else {
            tap->seekMs(0);
            tap->ring().discardAll();
            handoverPending = false;
            publishPosition(0, self.m_lengthMs.load());
        }
    }

    bool activate(const MediaRef& media, HistAction hist, std::size_t at) {
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (hist == HistAction::Append) {
                session.push_back(media);
                while (session.size() > kMaxSessionList) {
                    session.erase(session.begin());
                }
                sessionIndex = session.size() - 1;
            } else if (hist == HistAction::At) {
                sessionIndex = at;
            }
            current = media;
        }
        trackPrefetched.store(false);
        self.m_prefetchArmed.store(false);
        handoverPending = false;
        ended = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            pendingNext.reset();
        }
        // ReplayGain activation seam (AU-2): before the engine starts the media,
        // so the transform holds this track's gain for its first block.
        if (prepareHook) {
            prepareHook(media);
        }
        if (!engineStart(media)) {
            setState(PlaybackState::Stopped);
            return false;
        }
        forEachObserver([&](IPlaybackObserver& obs) { obs.onTrackChanged(media); });
        self.m_positionMs.store(0);
        self.m_lengthMs.store(0);
        lastPos = -1;
        lastLen = -1;
        setState(PlaybackState::Playing);
        return true;
    }

    void stopInternal() {
        ended = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            pendingNext.reset();
        }
        if (tap) {
            tap->stop();
            tap->ring().discardAll();
        }
        enginePlaying = false;
        trackPrefetched.store(false);
        self.m_prefetchArmed.store(false);
        handoverPending = false;
        const std::int64_t len = self.m_lengthMs.load();
        publishPosition(0, len);
        setState(PlaybackState::Stopped);
    }

    // =============================  engine  ================================

    bool ensureEngine() {
        if (tap) {
            return true;
        }
        tap = std::make_unique<VlcPcmTap>(ringFloats(config.ring));
        // libvlc threads only marshal into the mailbox; the dispatch thread does
        // everything else (PlaybackCore.hpp threading contract).
        tap->setEventListener([this](TapEvent event, std::int64_t value, std::uint64_t generation) {
            mailbox.push(event, value, generation);
            wakeup.notify_one();
        });
        applyPcmCallback();
        tap->setOutputGain(gainFor(self.m_volume100.load()));
        lastVolume = self.m_volume100.load();

        // AU-2: the decode-ahead depth is measured, not assumed — start from
        // "unknown" so the first boundary reserves only the floor.
        peakFillFrames = tap->ring().capacity() / kChannels;
        sink = std::make_unique<MiniAudioSink>(tap->ring());
        SinkConfig sinkConfig;
        sinkConfig.sampleRate = kSampleRate;
        sinkConfig.channels = kChannels;
        sinkConfig.periodFrames = config.periodFrames;
        sinkConfig.backend =
            config.output == PlaybackConfig::Output::Null ? SinkBackend::Null : SinkBackend::Auto;
        if (!sink->start(sinkConfig)) {
            sink.reset(); // tap-only: playback still runs, the ring will drop
            forEachObserver([&](IPlaybackObserver& obs) {
                obs.onError("miniaudio sink failed to start; the ring will drop PCM");
            });
        }
        engineReady.store(true, std::memory_order_release);
        return true;
    }

    void applyPcmCallback() {
        if (!tap) {
            return;
        }
        if (!pcmCb) {
            tap->setPcmObserver(nullptr);
            return;
        }
        const PcmCb cb = pcmCb;
        tap->setPcmObserver(
            [this, cb](const float* pcm, std::size_t frames, unsigned rate, unsigned channels) {
                if (pcmBlocked.load(std::memory_order_acquire)) {
                    return; // teardown: the transform may already be gone
                }
                pcmInFlight.fetch_add(1, std::memory_order_acq_rel);
                // Re-check: the gate may have closed between the load and the
                // increment; teardown waits for this counter, so it either sees
                // us or we see the gate.
                if (pcmBlocked.load(std::memory_order_acquire)) {
                    pcmInFlight.fetch_sub(1, std::memory_order_acq_rel);
                    return;
                }
                const PcmFormat format{static_cast<int>(rate), static_cast<int>(channels)};
                cb(pcm, frames, format);
                pcmInFlight.fetch_sub(1, std::memory_order_acq_rel);
            });
    }

    bool engineStart(const MediaRef& media) {
        if (!ensureEngine()) {
            return false;
        }
        tap->stop();
        tap->ring().discardAll(); // drop the previous media's tail deterministically
        const bool location = media.kind == MediaRef::Kind::Url;
        bool ok = false;
        if (media.kind == MediaRef::Kind::Stream) {
            // Callback-backed source (SV-2 direct play): the tap creates the
            // player on first use, so there is no separate open step.
            ok = tap->setMediaStream(media.stream);
            enginePrimed = true;
        } else if (!enginePrimed) {
            ok = location ? tap->openLocation(media.value) : tap->open(media.value);
            enginePrimed = true;
        } else {
            ok = location ? tap->setMediaLocation(media.value) : tap->setMedia(media.value);
        }
        if (!ok) {
            enginePlaying = false;
            const std::string what =
                "libvlc could not open media: " +
                (media.kind == MediaRef::Kind::Stream ? std::string("<stream>") : media.value);
            forEachObserver([&](IPlaybackObserver& obs) { obs.onError(what); });
            return false;
        }
        tap->play();
        enginePlaying = true;
        return true;
    }

    void stopThread() {
        {
            std::lock_guard<std::mutex> lock(cmdMutex);
            shutdown = true;
        }
        wakeup.notify_all();
        if (dispatch.joinable()) {
            dispatch.join();
        }
    }
};

// ===========================  public surface  =============================

PlaybackCore::PlaybackCore(PlaybackConfig config)
    : m_impl(std::make_unique<Impl>(*this, config)) {
    Impl* impl = m_impl.get();
    impl->dispatch = std::thread([impl] { impl->run(); });
}

PlaybackCore::~PlaybackCore() {
    m_impl->stopThread();
    // Quiescent now. Teardown order is load-bearing: the sink pulls the ring the
    // tap owns (FE-2 chain), and the tap's own destructor stops the libvlc
    // player — which can deliver events into this mailbox — so both are released
    // here, while every Impl member they touch is still alive, rather than by
    // member destruction order.
    m_impl->sink.reset();
    if (m_impl->tap) {
        // Close the PCM gate and wait (bounded) for an in-flight callback to
        // leave before the tap — and the scratch buffer the callback is handed —
        // is released. Bounded because a wedged callback must not hang shutdown;
        // the detach below then makes even a late call a no-op.
        m_impl->pcmBlocked.store(true, std::memory_order_release);
        for (int i = 0; i < 1000 && m_impl->pcmInFlight.load(std::memory_order_acquire) != 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // The PCM seam runs on libvlc's audio-output thread until the the tap is
        // gone: stop the player and detach the observer *before* the tap is
        // released, so a callback cannot reach the consumer's transform (or the
        // scratch it owns) while it is being destroyed (AU-2). An in-flight
        // callback is still possible; that is why the header requires the
        // callback's captures to outlive the core.
        m_impl->tap->stop();
        m_impl->tap->setPcmObserver(nullptr);
    }
    m_impl->tap.reset();
    {
        std::lock_guard<std::mutex> lock(m_impl->observerMutex);
        m_impl->observers.clear();
    }
}

void PlaybackCore::setPcmCallback(PcmCb cb) {
    m_impl->pcmCb = std::move(cb);
    m_impl->applyPcmCallback();
}

void PlaybackCore::play(const MediaRef& media) {
    Command command{Command::Type::Play};
    command.media = media;
    m_impl->request(std::move(command));
}

void PlaybackCore::pause() {
    m_impl->request(Command{Command::Type::Pause});
}

void PlaybackCore::resume() {
    m_impl->request(Command{Command::Type::Resume});
}

void PlaybackCore::togglePause() {
    if (m_state.load() == PlaybackState::Paused) {
        resume();
    } else {
        pause(); // a no-op unless actually playing
    }
}

void PlaybackCore::stop() {
    m_impl->request(Command{Command::Type::Stop});
}

void PlaybackCore::seekMs(std::int64_t ms) {
    Command command{Command::Type::Seek};
    command.i64 = std::max<std::int64_t>(0, ms);
    m_impl->request(std::move(command));
}

void PlaybackCore::setVolume(float volume) {
    setVolume100(static_cast<int>(std::lround(std::clamp(volume, 0.0f, 1.0f) * 100.0f)));
}

void PlaybackCore::setVolume100(int volume) {
    const int clamped = std::clamp(volume, 0, 100);
    m_volume100.store(clamped); // read-back immediately; gain applies on dispatch
    Command command{Command::Type::SetVolume};
    command.i = clamped;
    m_impl->request(std::move(command));
}

bool PlaybackCore::isPlaying() const {
    return m_state.load() == PlaybackState::Playing;
}

void PlaybackCore::next() {
    m_impl->request(Command{Command::Type::Next});
}

void PlaybackCore::previous() {
    m_impl->request(Command{Command::Type::Prev});
}

std::optional<MediaRef> PlaybackCore::currentMedia() const {
    return m_impl->currentLocked();
}

void PlaybackCore::enqueue(MediaRef media) {
    std::lock_guard<std::mutex> lock(m_impl->stateMutex);
    m_impl->queue.push_back(std::move(media));
    m_queueSize.store(m_impl->queue.size());
}

void PlaybackCore::clearQueue() {
    std::lock_guard<std::mutex> lock(m_impl->stateMutex);
    m_impl->queue.clear();
    m_queueSize.store(0);
}

std::optional<MediaRef> PlaybackCore::peekNext() const {
    return m_impl->peekNextInternal();
}

void PlaybackCore::setRepeat(RepeatMode mode) {
    m_repeat.store(static_cast<int>(mode));
}

void PlaybackCore::setShuffle(bool on) {
    m_shuffle.store(on);
}

void PlaybackCore::setPrefetchHook(PrefetchHook hook) {
    Command command{Command::Type::SetPrefetchHook};
    command.hook = std::move(hook);
    m_impl->request(std::move(command));
}

void PlaybackCore::setTrackPrepareHook(TrackPrepareHook hook) {
    Command command{Command::Type::SetTrackPrepareHook};
    command.prepareHook = std::move(hook);
    m_impl->request(std::move(command));
}

void PlaybackCore::addObserver(IPlaybackObserver* observer) {
    if (observer == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_impl->observerMutex);
    if (std::find(m_impl->observers.begin(), m_impl->observers.end(), observer) ==
        m_impl->observers.end()) {
        m_impl->observers.push_back(observer);
    }
}

void PlaybackCore::removeObserver(IPlaybackObserver* observer) {
    std::lock_guard<std::mutex> lock(m_impl->observerMutex);
    m_impl->observers.erase(
        std::remove(m_impl->observers.begin(), m_impl->observers.end(), observer),
        m_impl->observers.end());
}

PlaybackMetrics PlaybackCore::metrics() const {
    PlaybackMetrics out;
    out.state = m_state.load();
    out.positionMs = m_positionMs.load();
    out.lengthMs = m_lengthMs.load();
    out.volume100 = m_volume100.load();
    out.queueSize = m_queueSize.load();
    out.eventsDropped = m_impl->mailbox.dropped();
    if (m_impl->engineReady.load(std::memory_order_acquire)) {
        out.framesDecoded = m_impl->tap->stats().framesIn.load(std::memory_order_relaxed);
        out.ringFrames = m_impl->tap->ring().capacity() / kChannels;
        out.pcmRms = m_impl->tap->rmsLevel();
        out.framesDropped = m_impl->tap->stats().framesDropped.load(std::memory_order_relaxed);
        out.framesDiscarded = m_impl->tap->ring().discarded() / kChannels;
        out.silenceFrames = m_impl->sink ? m_impl->sink->stats().silenceFrames.load(std::memory_order_relaxed) : 0;
        out.gaplessHandovers = m_impl->handovers;
        out.lastBoundaryTailMs = m_impl->lastBoundaryTailMs;
        out.decodeAheadFrames = m_impl->peakFillFrames;
        out.nativeEqEnabled = m_impl->tap->hasNativeEqualizer();
        if (m_impl->sink) {
            out.framesRendered =
                m_impl->sink->stats().framesRendered.load(std::memory_order_relaxed);
            out.underruns = m_impl->sink->stats().underruns.load(std::memory_order_relaxed);
            out.outputBackend = m_impl->sink->backendName();
        }
    }
    return out;
}

} // namespace koalaamp::playback
