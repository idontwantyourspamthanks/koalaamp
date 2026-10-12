// SPDX-License-Identifier: MIT
//
// Transport — the playback surface the main window drives.
//
// `src/ui` never talks to libvlc: the bridge to `playback::PlaybackCore` is
// `PlaybackCoreTransport` behind this interface (docs/spec.md §2.2 — layers
// meet at abstract interfaces). Tests drive a fake, and the interaction-parity
// suite asserts both the calls a click produces and the pixels it renders.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "playback/IAudioEngine.hpp"
#include "playback/IPlaybackObserver.hpp"

namespace koalaamp::ui {

/// Media metadata the window displays beyond what the PCM tap reports: only
/// the bitrate is not observable from `IAudioEngine`, so it stays a plain
/// query (0 = unknown, rendered as Winamp's "  0" placeholder).
class ITransport {
public:
    virtual ~ITransport() = default;

    // --- commands -----------------------------------------------------------
    virtual void playMedia(const playback::MediaRef& media) = 0;
    /// Plays (or restarts) whatever media the engine already holds.
    virtual void resume() = 0;
    virtual void pause() = 0;
    virtual void togglePause() = 0;
    virtual void stop() = 0;
    virtual void next() = 0;
    virtual void previous() = 0;
    virtual void seekMs(std::int64_t positionMs) = 0;
    virtual void setVolumePercent(int volume) = 0;
    virtual void setShuffle(bool enabled) = 0;
    /// Winamp's repeat is a 3-state cycle: off, all, one.
    virtual void setRepeatMode(playback::RepeatMode mode) = 0;

    // --- queue (JTFE semantics; FE-4 PlaybackCore) --------------------------
    /// Appends a medium to the queue; the queue is consumed before the
    /// session-list/repeat/shuffle refill.
    virtual void enqueue(const playback::MediaRef& media) = 0;
    /// Empties the queue.
    virtual void clearQueue() = 0;
    /// Number of entries currently queued.
    virtual std::size_t queueSize() const = 0;
    /// Front of the queue, else the repeat/shuffle fallback candidate.
    virtual std::optional<playback::MediaRef> peekNext() const = 0;

    // --- state --------------------------------------------------------------
    virtual bool hasMedia() const = 0;
    virtual playback::PlaybackState state() const = 0;
    virtual std::int64_t positionMs() const = 0;
    virtual std::int64_t lengthMs() const = 0;
    virtual int volumePercent() const = 0;
    virtual bool shuffle() const = 0;
    virtual playback::RepeatMode repeatMode() const = 0;
    /// Display title of the loaded media (filename stem; tags are SE-3's).
    virtual std::string title() const = 0;
    /// The media the engine currently holds, if any. The playlist matches this
    /// against its entries to mark the playing row.
    virtual std::optional<playback::MediaRef> currentMedia() const = 0;
    /// Bitrate in kbit/s, 0 when unknown.
    virtual int bitrateKbps() const { return 0; }
};

}  // namespace koalaamp::ui
