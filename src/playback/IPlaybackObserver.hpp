// SPDX-License-Identifier: MIT
// playback — observer seam for PlaybackCore (FE-4).
//
// Qt-free by contract (docs/spec.md §2.1): src/ui adapts this interface to
// Qt signals in SE-2. PlaybackCore invokes every method on its own dispatch
// thread — NEVER on a libvlc thread (EndReached and friends arrive on VLC
// threads; the core marshals them). An observer may call back into the core.
// An observer MUST outlive the core (or be removed before destruction): the
// dispatch thread keeps calling out until the core's destructor joins it, so an
// observer declared after the core is destroyed first and can still be called
// (AU-2 measured exactly that as an invalid-vptr use-after-free under UBSAN).
// See PlaybackCore.hpp for the full threading contract.

#pragma once

#include "playback/IAudioEngine.hpp"

#include <cstdint>
#include <string_view>

namespace koalaamp::playback {

enum class PlaybackState {
    Stopped,
    Paused,
    Playing,
};

/// Queue/repeat state machine (minimal here; JTFE semantics are SE-3).
enum class RepeatMode {
    Off, ///< stop after the queue and the session list run out
    All, ///< wrap the session list
    One, ///< repeat the current track on natural end
};

class IPlaybackObserver {
public:
    virtual ~IPlaybackObserver() = default;

    /// PlaybackState transition (Stopped/Paused/Playing). Fires only on change.
    virtual void onStateChanged(PlaybackState /*state*/) {}
    /// A new media was activated (play/next/prev/queue pop/auto-advance).
    virtual void onTrackChanged(const MediaRef& /*media*/) {}
    /// Position tick (~100 Hz while a media is loaded); lengthMs is 0 until known.
    virtual void onPositionChanged(std::int64_t /*positionMs*/, std::int64_t /*lengthMs*/) {}
    /// Volume changed, 0..100 perceptual (docs/spec.md §3.3).
    virtual void onVolumeChanged(int /*volume100*/) {}
    /// The current media reached its natural end, before auto-advance. `ended`
    /// is the media that finished; onTrackChanged for the next one follows.
    virtual void onEndReached(const MediaRef& /*ended*/) {}
    /// The next media was resolved `prefetchLeadMs` before the end (spec §3.2's
    /// scheduler prefetch). Resolution only: the boundary itself is the core's
    /// (AU-2) and is reported as `onTrackChanged` when it is taken.
    virtual void onPrefetch(const MediaRef& /*next*/) {}
    /// Non-fatal engine error (bad media, libvlc error, sink start failure).
    virtual void onError(std::string_view /*what*/) {}
};

} // namespace koalaamp::playback
