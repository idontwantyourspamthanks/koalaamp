// SPDX-License-Identifier: MIT
// jellyfin — player wiring for `/Sessions/Playing*`. See the header.

#include "servers/jellyfin/JellyfinPlaybackReporter.hpp"

#include <cstdlib>
#include <random>
#include <utility>
#include <variant>

namespace koalaamp::servers::jellyfin {
namespace {

std::string newSessionId() {
    static std::random_device device;
    static constexpr char hex[] = "0123456789abcdef";
    std::string id;
    id.reserve(32);
    for (int i = 0; i < 16; ++i) {
        const unsigned value = device() & 0xffu;
        id.push_back(hex[value >> 4]);
        id.push_back(hex[value & 0x0fu]);
    }
    return id;
}

} // namespace

JellyfinPlaybackReporter::JellyfinPlaybackReporter(JellyfinClient& client, std::string baseUrl,
                                                   std::string token, std::string deviceId)
    : client_(client),
      baseUrl_(std::move(baseUrl)),
      token_(std::move(token)),
      deviceId_(std::move(deviceId)) {}

std::string JellyfinPlaybackReporter::playSessionId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (playSessionId_.empty()) {
        playSessionId_ = newSessionId();
    }
    return playSessionId_;
}

JellyfinPlaybackReporter::Pending
JellyfinPlaybackReporter::makeLocked(PlaybackEvent event, std::string eventName) const {
    Pending pending;
    pending.event = event;
    pending.report.itemId = current_->itemId;
    pending.report.mediaSourceId = current_->mediaSourceId;
    pending.report.playSessionId = playSessionId_;
    pending.report.playMethod = current_->playMethod;
    pending.report.positionTicks = positionMs_ * 10000; // Jellyfin ticks (100 ns)
    pending.report.paused = paused_;
    pending.report.muted = muted_;
    pending.report.volumeLevel = volume100_;
    pending.report.eventName = std::move(eventName);
    return pending;
}

void JellyfinPlaybackReporter::enqueueLocked(PlaybackEvent event, std::string eventName) {
    if (!current_) {
        return;
    }
    pending_.push_back(makeLocked(event, std::move(eventName)));
    // A server that stopped answering must not grow the queue without bound;
    // dropping the oldest progress beats dropping the eventual Stop.
    constexpr std::size_t kMaxPending = 16;
    while (pending_.size() > kMaxPending) {
        pending_.pop_front();
    }
}

void JellyfinPlaybackReporter::onTrackChanged(const koalaamp::playback::MediaRef& media) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ && current_) {
        // Gapless boundary or explicit advance: close the old item within the
        // same queue session, then open the new one under the same id.
        enqueueLocked(PlaybackEvent::Stop, {});
    }
    if (!media.server) {
        active_ = false;
        current_.reset();
        playSessionId_.clear();
        return;
    }
    if (playSessionId_.empty()) {
        playSessionId_ = newSessionId();
        lastHeartbeatMs_ = -1;
    }
    current_ = *media.server;
    positionMs_ = 0;
    lengthMs_ = 0;
    paused_ = false;
    active_ = true;
    enqueueLocked(PlaybackEvent::Start, {});
}

void JellyfinPlaybackReporter::onStateChanged(koalaamp::playback::PlaybackState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !current_) {
        return;
    }
    if (state == koalaamp::playback::PlaybackState::Paused && !paused_) {
        paused_ = true;
        enqueueLocked(PlaybackEvent::Progress, "Pause");
    } else if (state == koalaamp::playback::PlaybackState::Playing && paused_) {
        paused_ = false;
        enqueueLocked(PlaybackEvent::Progress, "Unpause");
    } else if (state == koalaamp::playback::PlaybackState::Stopped) {
        enqueueLocked(PlaybackEvent::Stop, {});
        active_ = false;
        current_.reset();
        playSessionId_.clear();
    }
}

void JellyfinPlaybackReporter::onPositionChanged(std::int64_t positionMs,
                                                 std::int64_t lengthMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) {
        return;
    }
    if (lengthMs > 0) {
        lengthMs_ = lengthMs;
    }
    const std::int64_t previous = positionMs_;
    positionMs_ = positionMs;
    // Position arrives at ~100 Hz, so a jump bigger than two seconds is a seek,
    // not playback drift. One pending Seek is enough per gesture.
    if (previous > 0 && std::llabs(positionMs - previous) > 2000) {
        bool seekPending = false;
        for (const auto& pending : pending_) {
            if (pending.report.eventName == "Seek") {
                seekPending = true;
                break;
            }
        }
        if (!seekPending) {
            enqueueLocked(PlaybackEvent::Progress, "Seek");
        }
    }
}

void JellyfinPlaybackReporter::onVolumeChanged(int volume100) {
    std::lock_guard<std::mutex> lock(mutex_);
    volume100_ = volume100 < 0 ? 0 : (volume100 > 100 ? 100 : volume100);
    if (active_) {
        enqueueLocked(PlaybackEvent::Progress, "VolumeChange");
    }
}

void JellyfinPlaybackReporter::tick(std::int64_t nowMs) {
    std::deque<Pending> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        constexpr std::size_t kMaxBatch = 8;
        while (!pending_.empty() && out.size() < kMaxBatch) {
            out.push_back(std::move(pending_.front()));
            pending_.pop_front();
        }
        if (active_ && current_) {
            if (lastHeartbeatMs_ < 0) {
                lastHeartbeatMs_ = nowMs; // first heartbeat is one interval away
            } else if (nowMs - lastHeartbeatMs_ >= 10000) {
                lastHeartbeatMs_ = nowMs;
                out.push_back(makeLocked(PlaybackEvent::Progress, "TimeUpdate"));
            }
        }
    }

    for (Pending& pending : out) {
        const ReportOutcome outcome =
            client_.reportPlayback(baseUrl_, token_, deviceId_, pending.event, pending.report);
        std::lock_guard<std::mutex> lock(mutex_);
        if (std::holds_alternative<ReportSuccess>(outcome)) {
            if (pending.event == PlaybackEvent::Start) {
                ++stats_.starts;
            } else if (pending.event == PlaybackEvent::Stop) {
                ++stats_.stops;
            } else {
                ++stats_.progress;
            }
        } else if (std::holds_alternative<ItemsUnauthorized>(outcome)) {
            stats_.lastError = "The server rejected the token";
        } else if (std::holds_alternative<ItemsUnreachable>(outcome)) {
            stats_.lastError = "Server unreachable";
        } else {
            stats_.lastError = std::get<ItemsFailed>(outcome).message;
        }
    }
}

void JellyfinPlaybackReporter::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
    active_ = false;
    current_.reset();
    playSessionId_.clear();
    lastHeartbeatMs_ = -1;
}

JellyfinPlaybackReporter::Stats JellyfinPlaybackReporter::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

} // namespace koalaamp::servers::jellyfin
