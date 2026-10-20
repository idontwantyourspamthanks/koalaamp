// SPDX-License-Identifier: MIT
// jellyfin — the player half of `/Sessions/Playing*` reporting (SV-2).
//
// `JellyfinClient::reportPlayback` owns the wire shape; this class owns *when*:
// Start on the first server track of a queue session, Progress every 10 s and
// immediately on pause/unpause/seek/volume/track change, Stop when the session
// ends. The PlaySessionId is generated once per queue session and reused across
// gapless track changes.
//
// Threading: the observer callbacks run on PlaybackCore's dispatch thread, so
// they only record state and queue reports under a mutex. All HTTP happens in
// `tick()` — call it from the GUI timer. A blocked server therefore delays
// reports, never transport control.

#pragma once

#include "playback/IPlaybackObserver.hpp"
#include "servers/jellyfin/JellyfinClient.hpp"

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

namespace koalaamp::servers::jellyfin {

class JellyfinPlaybackReporter final : public koalaamp::playback::IPlaybackObserver {
public:
    struct Stats {
        int starts{0};
        int progress{0};
        int stops{0};
        std::string lastError;
    };

    JellyfinPlaybackReporter(JellyfinClient& client, std::string baseUrl, std::string token,
                             std::string deviceId);

    /// One PlaySessionId per queue session; pass it to
    /// `JellyfinPlayback::plan()` so a transcode URL carries the same value.
    std::string playSessionId() const;

    // --- IPlaybackObserver (dispatch thread; no I/O) -------------------------
    void onStateChanged(koalaamp::playback::PlaybackState state) override;
    void onTrackChanged(const koalaamp::playback::MediaRef& media) override;
    void onPositionChanged(std::int64_t positionMs, std::int64_t lengthMs) override;
    void onVolumeChanged(int volume100) override;

    // --- control thread ------------------------------------------------------
    /// Drains queued reports and emits the 10 s TimeUpdate heartbeat.
    void tick(std::int64_t nowMs);
    /// Forget the active session without reporting (sign-out/shutdown).
    void reset();

    Stats stats() const;

private:
    struct Pending {
        PlaybackEvent event{PlaybackEvent::Progress};
        PlaybackReport report;
    };

    /// Builds a report from the current state. Caller holds the mutex.
    Pending makeLocked(PlaybackEvent event, std::string eventName) const;
    /// Queues a report for the current item. Caller holds the mutex.
    void enqueueLocked(PlaybackEvent event, std::string eventName);

    JellyfinClient& client_;
    std::string baseUrl_;
    std::string token_;
    std::string deviceId_;

    mutable std::mutex mutex_;
    std::deque<Pending> pending_;
    std::optional<koalaamp::playback::ServerMediaInfo> current_;
    mutable std::string playSessionId_;
    std::int64_t positionMs_{0};
    std::int64_t lengthMs_{0};
    std::int64_t lastHeartbeatMs_{-1};
    int volume100_{100};
    bool paused_{false};
    bool muted_{false};
    bool active_{false};
    bool sessionEnded_{true};
    mutable Stats stats_;
};

} // namespace koalaamp::servers::jellyfin
