// SPDX-License-Identifier: MIT
// scrobble — PlaybackCore observer adapter (SC-1 app wiring).
//
// Converts the core's media/position/end events into ScrobbleService commands.
// Runs on PlaybackCore's dispatch thread, so it only enqueues work; the HTTP
// calls run on ScrobbleService's worker. Local files without MediaRef metadata
// are resolved once through the caller's TrackResolver (TagLib).

#pragma once

#include "library/ITrackSource.hpp"
#include "playback/IPlaybackObserver.hpp"
#include "scrobble/ScrobbleService.hpp"
#include "scrobble/ScrobbleTrack.hpp"

#include <cstdint>
#include <ctime>
#include <functional>
#include <optional>
#include <utility>

namespace koalaamp::scrobble {

inline ScrobbleTrack scrobbleTrackFrom(const library::Track& track) {
    ScrobbleTrack out;
    out.artist = track.artist;
    out.title = track.title;
    out.album = track.album;
    out.albumArtist = track.albumArtist;
    out.durationMs = static_cast<int>(track.durationMs);
    out.trackNumber = track.trackNo;
    out.recordingMbid = track.recordingMbid;
    out.artistMbids = track.artistMbids;
    out.releaseMbid = track.releaseMbid;
    return out;
}

class PlaybackScrobbler final : public playback::IPlaybackObserver {
public:
    using ModeProvider = std::function<ScrobbleMode()>;
    using TrackResolver = std::function<std::optional<library::Track>(const playback::MediaRef&)>;

    PlaybackScrobbler(ScrobbleService& service, ModeProvider mode, TrackResolver resolve)
        : service_(service), mode_(std::move(mode)), resolve_(std::move(resolve)) {}

    void onTrackChanged(const playback::MediaRef& media) override {
        service_.onStopped();
        lastPositionMs_ = -1;
        service_.setMode(mode_());
        const std::optional<library::Track> track = resolve_(media);
        if (!track) {
            return;
        }
        const ScrobbleTrack scrobble = scrobbleTrackFrom(*track);
        if (!scrobble.valid()) {
            return;
        }
        service_.onTrackStarted(scrobble, static_cast<std::int64_t>(std::time(nullptr)));
    }

    void onPositionChanged(std::int64_t positionMs, std::int64_t lengthMs) override {
        if (lastPositionMs_ >= 0 && positionMs >= lastPositionMs_ &&
            positionMs - lastPositionMs_ < 1000) {
            return;
        }
        lastPositionMs_ = positionMs;
        service_.onProgress(positionMs, lengthMs);
    }

    void onEndReached(const playback::MediaRef& /*ended*/) override { service_.onTrackEnded(); }

    void onStateChanged(playback::PlaybackState state) override {
        if (state == playback::PlaybackState::Stopped) {
            service_.onStopped();
        }
    }

private:
    ScrobbleService& service_;
    ModeProvider mode_;
    TrackResolver resolve_;
    std::int64_t lastPositionMs_{-1};
};

} // namespace koalaamp::scrobble
