// SPDX-License-Identifier: MIT
// scrobble — listen policy and submission coordinator. See ScrobbleCoordinator.hpp.

#include "scrobble/ScrobbleCoordinator.hpp"

#include "scrobble/ScrobbleQueue.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace koalaamp::scrobble {
namespace {

constexpr std::int64_t kFourMinutesMs = 4 * 60 * 1000;
constexpr std::size_t kLastFmBatchLimit = 50;

} // namespace

std::int64_t ScrobbleCoordinator::thresholdMs(std::int64_t durationMs) {
    if (durationMs <= 0) {
        return kFourMinutesMs;
    }
    return std::min<std::int64_t>(kFourMinutesMs, durationMs / 2);
}

void ScrobbleCoordinator::onTrackStarted(const ScrobbleTrack& track,
                                         std::int64_t startedAtUnix) {
    if (!track.valid()) {
        play_ = PlayState{};
        return;
    }
    if (play_.active && !play_.ended && sameTrackIdentity(play_.track, track)) {
        // Restart/seek within the same playback session. Keep the high-water
        // mark and the exactly-once flag; a repeat of the same track is a new
        // listen only after onTrackEnded()/onStopped().
        return;
    }

    play_ = PlayState{};
    play_.active = true;
    play_.track = track;
    play_.startedAtUnix = startedAtUnix;
    play_.durationMs = track.durationMs;
    sendNowPlaying();
}

void ScrobbleCoordinator::onProgress(std::int64_t positionMs, std::int64_t lengthMs) {
    if (!play_.active || play_.ended || play_.scrobbled) {
        return;
    }
    if (lengthMs > 0) {
        play_.durationMs = lengthMs;
    }
    play_.maxPositionMs = std::max(play_.maxPositionMs, std::max<std::int64_t>(0, positionMs));
    const std::int64_t duration =
        play_.durationMs > 0 ? play_.durationMs : static_cast<std::int64_t>(play_.track.durationMs);
    if (play_.maxPositionMs >= thresholdMs(duration)) {
        submitListen();
    }
}

void ScrobbleCoordinator::onTrackEnded() {
    if (!play_.active || play_.ended) {
        return;
    }
    const std::int64_t duration =
        play_.durationMs > 0 ? play_.durationMs : static_cast<std::int64_t>(play_.track.durationMs);
    if (duration > 0) {
        // A natural end is a full listen; a short last tick or a duration tag
        // that was too long must not lose it.
        play_.maxPositionMs = std::max(play_.maxPositionMs, duration);
    }
    if (!play_.scrobbled && play_.maxPositionMs >= thresholdMs(duration)) {
        submitListen();
    }
    play_.ended = true;
}

void ScrobbleCoordinator::onStopped() {
    if (!play_.active) {
        return;
    }
    play_.ended = true;
}

void ScrobbleCoordinator::sendNowPlaying() {
    if (mode_ != ScrobbleMode::Client) {
        return;
    }
    if (lastFm_ != nullptr) {
        lastFm_->updateNowPlaying(play_.track);
    }
    if (listenBrainz_ != nullptr) {
        listenBrainz_->submitPlayingNow(play_.track);
    }
}

void ScrobbleCoordinator::submitListen() {
    if (play_.scrobbled) {
        return;
    }
    play_.scrobbled = true;
    if (mode_ != ScrobbleMode::Client) {
        return;
    }

    const TimedScrobble entry{play_.track, play_.startedAtUnix};
    if (lastFm_ != nullptr && !lastFm_->scrobble({entry})) {
        queueListen(ListenService::LastFm, entry);
    }
    if (listenBrainz_ != nullptr &&
        !listenBrainz_->submitSingle(entry.track, entry.timestampUnix)) {
        queueListen(ListenService::ListenBrainz, entry);
    }
}

void ScrobbleCoordinator::queueListen(ListenService service, const TimedScrobble& entry) {
    if (queue_ == nullptr) {
        return;
    }
    PendingScrobble pending;
    pending.service = service;
    pending.track = entry.track;
    pending.timestampUnix = entry.timestampUnix;
    pending.dedupKey = listenDedupKey(service, entry.track, entry.timestampUnix);
    if (queue_->enqueue(std::move(pending))) {
        queue_->save();
    }
}

int ScrobbleCoordinator::flushPending() {
    if (mode_ != ScrobbleMode::Client || queue_ == nullptr) {
        return 0;
    }
    const std::vector<PendingScrobble> entries = queue_->entries();
    if (entries.empty()) {
        return 0;
    }

    int accepted = 0;
    std::vector<std::string> submitted;
    std::vector<std::string> failed;

    if (lastFm_ != nullptr) {
        std::vector<TimedScrobble> batch;
        const auto flushBatch = [&]() {
            if (batch.empty()) {
                return;
            }
            if (lastFm_->scrobble(batch)) {
                accepted += static_cast<int>(batch.size());
                for (const auto& entry : batch) {
                    submitted.push_back(
                        listenDedupKey(ListenService::LastFm, entry.track, entry.timestampUnix));
                }
            } else {
                for (const auto& entry : batch) {
                    failed.push_back(
                        listenDedupKey(ListenService::LastFm, entry.track, entry.timestampUnix));
                }
            }
            batch.clear();
        };
        for (const auto& entry : entries) {
            if (entry.service != ListenService::LastFm) {
                continue;
            }
            batch.push_back(TimedScrobble{entry.track, entry.timestampUnix});
            if (batch.size() >= kLastFmBatchLimit) {
                flushBatch();
            }
        }
        flushBatch();
    }

    if (listenBrainz_ != nullptr) {
        for (const auto& entry : entries) {
            if (entry.service != ListenService::ListenBrainz) {
                continue;
            }
            if (listenBrainz_->submitSingle(entry.track, entry.timestampUnix)) {
                ++accepted;
                submitted.push_back(entry.dedupKey);
            } else {
                failed.push_back(entry.dedupKey);
            }
        }
    }

    for (const auto& key : submitted) {
        queue_->remove(key);
    }
    for (const auto& key : failed) {
        queue_->recordAttempt(key);
    }
    if (!submitted.empty() || !failed.empty()) {
        queue_->save();
    }
    return accepted;
}

std::size_t ScrobbleCoordinator::pendingCount() const {
    return queue_ != nullptr ? queue_->size() : 0;
}

} // namespace koalaamp::scrobble
