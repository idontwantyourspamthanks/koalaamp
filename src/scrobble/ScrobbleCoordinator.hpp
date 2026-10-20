// SPDX-License-Identifier: MIT
// scrobble — listen policy and submission coordinator.
//
// This is the client-side scrobbler, independent of the server plugins. It takes
// explicit playback observations (start / progress / end / stop) rather than
// linking PlaybackCore: the UI or PlaybackCore adapter calls these methods, and
// SC-1's dependency on SV-2 stays conceptual.
//
// Policy (docs/spec.md §8.4, ticket SC-1, and both services' submission rules):
//   * a listen counts once the track reached 50 % of its length or 4 minutes,
//     whichever is lower (4 minutes when the length is unknown);
//   * exactly one submission per play, even across seeks and restarts;
//   * `track.updateNowPlaying` / ListenBrainz `playing_now` fire on start;
//   * a failed submission goes to the persisted queue and is retried by
//     flushPending(), deduplicated by service + artist/title + start timestamp;
//   * mode Relay means the server's plugin already scrobbles; the client
//     suppresses every now-playing and submission. Mode Off is the default.

#pragma once

#include "scrobble/ScrobbleTrack.hpp"
#include "scrobble/ScrobbleSubmitter.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace koalaamp::scrobble {

class ScrobbleQueue;

enum class ScrobbleMode {
    Off,    ///< no client scrobbling
    Client, ///< submit from this client
    Relay,  ///< server plugin already submits; suppress the client path
};

class ScrobbleCoordinator {
public:
    ScrobbleCoordinator() = default;

    void setMode(ScrobbleMode mode) { mode_ = mode; }
    ScrobbleMode mode() const { return mode_; }

    void setLastFm(ILastFmSubmitter* lastFm) { lastFm_ = lastFm; }
    void setListenBrainz(IListenBrainzSubmitter* listenBrainz) {
        listenBrainz_ = listenBrainz;
    }
    void setQueue(ScrobbleQueue* queue) { queue_ = queue; }

    /// Starts a play. `startedAtUnix` is the timestamp the services record for
    /// the listen. Repeating the same artist/title/album before onTrackEnded()
    /// or onStopped() is a seek/restart, so it neither re-sends now-playing nor
    /// re-arms the submission.
    void onTrackStarted(const ScrobbleTrack& track, std::int64_t startedAtUnix);

    /// Position tick. Only the high-water position matters, so seeking forward
    /// past the threshold and back again cannot submit twice.
    void onProgress(std::int64_t positionMs, std::int64_t lengthMs);

    /// Natural end: a full play, even if metadata length was wrong or the last
    /// tick arrived early. The next start — same track or not — is a new play.
    void onTrackEnded();

    /// User stop/skip: closes the play without adding a listen. Anything already
    /// submitted at the threshold stays submitted.
    void onStopped();

    /// Retries queued listens for the configured clients in Last.fm-sized
    /// batches. Returns the number accepted and removed from the queue.
    int flushPending();

    std::size_t pendingCount() const;
    bool currentPlayScrobbled() const { return play_.scrobbled; }

    /// 50 % of the duration, capped at 4 minutes; 4 minutes when unknown.
    static std::int64_t thresholdMs(std::int64_t durationMs);

private:
    struct PlayState {
        bool active{false};
        bool ended{false};
        bool scrobbled{false};
        ScrobbleTrack track;
        std::int64_t startedAtUnix{0};
        std::int64_t durationMs{0};
        std::int64_t maxPositionMs{0};
    };

    void sendNowPlaying();
    void submitListen();
    void queueListen(ListenService service, const TimedScrobble& entry);

    ScrobbleMode mode_{ScrobbleMode::Off};
    ILastFmSubmitter* lastFm_{nullptr};
    IListenBrainzSubmitter* listenBrainz_{nullptr};
    ScrobbleQueue* queue_{nullptr};
    PlayState play_;
};

} // namespace koalaamp::scrobble
