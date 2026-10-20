// SPDX-License-Identifier: MIT
// plex — `/:/timeline`, watched state, and the player wiring (SV-4).
//
// The wire shape is koalapaw `PlexPlayback.reportTimeline`: POST with query
// params (ratingKey, key=/library/metadata/{id}, state, time ms, duration ms),
// token in the header. `PlexPlaybackReporter` follows the SV-2 pattern: the
// observer callbacks only record state; `tick()` does the HTTP, so playback
// never blocks on the dashboard.
//
// Watched state ports koalapaw's GET-on-ambient-token behavior verbatim
// (`PlexDetail.kt` P3 note: the GET/PUT verb was never verified live). The live
// PMS pass in SV-4's ticket is where that gets pinned.

#pragma once

#include "playback/IPlaybackObserver.hpp"
#include "servers/jellyfin/HttpTransport.hpp"
#include "servers/plex/PlexTypes.hpp"

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace koalaamp::servers::plex {

enum class PlexTimelineState { Playing, Paused, Stopped };

struct PlexTimelineEntry {
    std::string ratingKey;
    std::string key; ///< `/library/metadata/{ratingKey}`
    PlexTimelineState state{PlexTimelineState::Playing};
    std::int64_t timeMs{0};
    std::int64_t durationMs{0};
};

class PlexTimeline {
public:
    PlexTimeline(jellyfin::IHttpTransport& transport, PlexClientInfo info, PlexSession session);

    bool report(const PlexTimelineEntry& entry,
                const library::Cancellation& cancel = jellyfin::NoCancellation{});
    /// `GET /:/scrobble` and `/:/unscrobble` (verb pinned in the live pass).
    bool scrobble(std::string_view ratingKey,
                  const library::Cancellation& cancel = jellyfin::NoCancellation{});
    bool unscrobble(std::string_view ratingKey,
                    const library::Cancellation& cancel = jellyfin::NoCancellation{});

    const std::string& lastError() const { return lastError_; }

private:
    bool watched(std::string_view path, std::string_view ratingKey,
                 const library::Cancellation& cancel);

    jellyfin::IHttpTransport& transport_;
    PlexClientInfo info_;
    PlexSession session_;
    std::string lastError_;
};

/// Observer adapter: queues start/pause/unpause/stop and the 10 s heartbeat,
/// then reports from `tick()` on the control thread.
class PlexPlaybackReporter final : public koalaamp::playback::IPlaybackObserver {
public:
    struct Stats {
        int reports{0};
        std::string lastError;
    };

    PlexPlaybackReporter(jellyfin::IHttpTransport& transport, PlexClientInfo info,
                         PlexSession session);

    void onStateChanged(koalaamp::playback::PlaybackState state) override;
    void onTrackChanged(const koalaamp::playback::MediaRef& media) override;
    void onPositionChanged(std::int64_t positionMs, std::int64_t lengthMs) override;

    void tick(std::int64_t nowMs);
    bool markPlayed();
    bool markUnplayed();
    std::string currentRatingKey() const;
    Stats stats() const;
    void reset();

private:
    void enqueueLocked(PlexTimelineState state);

    PlexTimeline timeline_;
    mutable std::mutex mutex_;
    std::deque<PlexTimelineEntry> pending_;
    std::optional<koalaamp::playback::ServerMediaInfo> current_;
    std::int64_t positionMs_{0};
    std::int64_t durationMs_{0};
    std::int64_t lastHeartbeatMs_{-1};
    bool active_{false};
    bool paused_{false};
    mutable Stats stats_;
};

} // namespace koalaamp::servers::plex
