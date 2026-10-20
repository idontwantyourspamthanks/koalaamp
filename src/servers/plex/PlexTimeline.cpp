// SPDX-License-Identifier: MIT
// plex — timeline/watched reporting and player wiring. See PlexTimeline.hpp.

#include "servers/plex/PlexTimeline.hpp"

#include <utility>

namespace koalaamp::servers::plex {
namespace {

using jellyfin::HttpRequest;
using jellyfin::HttpResponse;

std::string baseNoSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

std::string encodeQuery(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char c : value) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                                c == '~';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0f]);
        }
    }
    return out;
}

const char* stateName(PlexTimelineState state) {
    switch (state) {
        case PlexTimelineState::Paused:
            return "paused";
        case PlexTimelineState::Stopped:
            return "stopped";
        case PlexTimelineState::Playing:
        default:
            return "playing";
    }
}

bool httpOk(const HttpResponse& response, std::string& error) {
    if (!response.transportOk()) {
        error = response.error;
        return false;
    }
    if (response.status == 401) {
        error = "The Plex server rejected the token";
        return false;
    }
    if (!response.httpOk()) {
        error = "Plex responded with HTTP " + std::to_string(response.status);
        return false;
    }
    error.clear();
    return true;
}

} // namespace

PlexTimeline::PlexTimeline(jellyfin::IHttpTransport& transport, PlexClientInfo info,
                           PlexSession session)
    : transport_(transport), info_(std::move(info)), session_(std::move(session)) {}

bool PlexTimeline::report(const PlexTimelineEntry& entry,
                          const library::Cancellation& cancel) {
    std::string url = baseNoSlash(session_.baseUrl) + "/:/timeline?ratingKey=" +
                      encodeQuery(entry.ratingKey) + "&key=" + encodeQuery(entry.key) +
                      "&state=" + stateName(entry.state) +
                      "&time=" + std::to_string(entry.timeMs < 0 ? 0 : entry.timeMs);
    if (entry.durationMs > 0) {
        url += "&duration=" + std::to_string(entry.durationMs);
    }
    HttpRequest request;
    request.method = "POST";
    request.url = std::move(url);
    request.headers = plexHeaders(info_, session_.token);
    request.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    const HttpResponse response = transport_.perform(request, cancel);
    return httpOk(response, lastError_);
}

bool PlexTimeline::watched(std::string_view path, std::string_view ratingKey,
                           const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "GET";
    request.url = baseNoSlash(session_.baseUrl) + std::string(path) +
                  "?key=" + encodeQuery(ratingKey) +
                  "&identifier=com.plexapp.plugins.library";
    request.headers = plexHeaders(info_, session_.token);
    const HttpResponse response = transport_.perform(request, cancel);
    return httpOk(response, lastError_);
}

bool PlexTimeline::scrobble(std::string_view ratingKey,
                            const library::Cancellation& cancel) {
    return watched("/:/scrobble", ratingKey, cancel);
}

bool PlexTimeline::unscrobble(std::string_view ratingKey,
                              const library::Cancellation& cancel) {
    return watched("/:/unscrobble", ratingKey, cancel);
}

PlexPlaybackReporter::PlexPlaybackReporter(jellyfin::IHttpTransport& transport,
                                           PlexClientInfo info, PlexSession session)
    : timeline_(transport, std::move(info), std::move(session)) {}

void PlexPlaybackReporter::enqueueLocked(PlexTimelineState state) {
    if (!current_) {
        return;
    }
    pending_.push_back(PlexTimelineEntry{current_->itemId,
                                         "/library/metadata/" + current_->itemId, state,
                                         positionMs_, durationMs_});
    constexpr std::size_t kMaxPending = 16;
    while (pending_.size() > kMaxPending) {
        pending_.pop_front();
    }
}

void PlexPlaybackReporter::onTrackChanged(const koalaamp::playback::MediaRef& media) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ && current_) {
        enqueueLocked(PlexTimelineState::Stopped);
    }
    if (!media.server) {
        active_ = false;
        current_.reset();
        positionMs_ = 0;
        durationMs_ = 0;
        return;
    }
    current_ = *media.server;
    active_ = true;
    paused_ = false;
    positionMs_ = 0;
    durationMs_ = 0;
    lastHeartbeatMs_ = -1;
    enqueueLocked(PlexTimelineState::Playing);
}

void PlexPlaybackReporter::onStateChanged(koalaamp::playback::PlaybackState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || !current_) {
        return;
    }
    if (state == koalaamp::playback::PlaybackState::Paused && !paused_) {
        paused_ = true;
        enqueueLocked(PlexTimelineState::Paused);
    } else if (state == koalaamp::playback::PlaybackState::Playing && paused_) {
        paused_ = false;
        enqueueLocked(PlexTimelineState::Playing);
    } else if (state == koalaamp::playback::PlaybackState::Stopped) {
        enqueueLocked(PlexTimelineState::Stopped);
        active_ = false;
    }
}

void PlexPlaybackReporter::onPositionChanged(std::int64_t positionMs,
                                             std::int64_t lengthMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    positionMs_ = positionMs;
    if (lengthMs > 0) {
        durationMs_ = lengthMs;
    }
}

void PlexPlaybackReporter::tick(std::int64_t nowMs) {
    std::deque<PlexTimelineEntry> out;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        constexpr std::size_t kMaxBatch = 8;
        while (!pending_.empty() && out.size() < kMaxBatch) {
            out.push_back(std::move(pending_.front()));
            pending_.pop_front();
        }
        if (active_ && current_) {
            if (lastHeartbeatMs_ < 0) {
                lastHeartbeatMs_ = nowMs;
            } else if (nowMs - lastHeartbeatMs_ >= 10000) {
                lastHeartbeatMs_ = nowMs;
                out.push_back(PlexTimelineEntry{
                    current_->itemId, "/library/metadata/" + current_->itemId,
                    paused_ ? PlexTimelineState::Paused : PlexTimelineState::Playing,
                    positionMs_, durationMs_});
            }
        }
    }

    for (const PlexTimelineEntry& entry : out) {
        const bool ok = timeline_.report(entry);
        std::lock_guard<std::mutex> lock(mutex_);
        if (ok) {
            ++stats_.reports;
        } else {
            stats_.lastError = timeline_.lastError();
        }
    }
}

std::string PlexPlaybackReporter::currentRatingKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_ ? current_->itemId : std::string{};
}

bool PlexPlaybackReporter::markPlayed() {
    const std::string key = currentRatingKey();
    if (key.empty()) {
        return false;
    }
    const bool ok = timeline_.scrobble(key);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ok) {
        stats_.lastError = timeline_.lastError();
    }
    return ok;
}

bool PlexPlaybackReporter::markUnplayed() {
    const std::string key = currentRatingKey();
    if (key.empty()) {
        return false;
    }
    const bool ok = timeline_.unscrobble(key);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ok) {
        stats_.lastError = timeline_.lastError();
    }
    return ok;
}

PlexPlaybackReporter::Stats PlexPlaybackReporter::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void PlexPlaybackReporter::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
    current_.reset();
    active_ = false;
    paused_ = false;
    positionMs_ = 0;
    durationMs_ = 0;
    lastHeartbeatMs_ = -1;
}

} // namespace koalaamp::servers::plex
