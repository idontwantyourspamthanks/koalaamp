// SPDX-License-Identifier: MIT
// scrobble — the listen shape shared by Last.fm, ListenBrainz and the queue.
//
// Field names follow the Last.fm submission vocabulary (artist, track title,
// album, albumArtist, duration). MusicBrainz IDs are carried when the server
// supplied them (Jellyfin ProviderIds): Last.fm uses `recordingMbid` as `mbid`,
// ListenBrainz puts all three in `track_metadata.additional_info`.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace koalaamp::scrobble {

enum class ListenService { LastFm, ListenBrainz };

struct ScrobbleTrack {
    std::string artist;
    std::string title;
    std::string album;
    std::string albumArtist;
    /// Length in milliseconds; 0 when unknown. Last.fm takes whole seconds.
    int durationMs{0};
    int trackNumber{0};
    std::string recordingMbid;
    std::vector<std::string> artistMbids;
    std::string releaseMbid;

    /// The two fields every scrobbler requires.
    bool valid() const { return !artist.empty() && !title.empty(); }
};

/// One listen plus the time it started (Unix seconds), which is the timestamp
/// Last.fm expects for `track.scrobble` and ListenBrainz for `listened_at`.
struct TimedScrobble {
    ScrobbleTrack track;
    std::int64_t timestampUnix{0};
};

/// Identity for the coordinator's exactly-once rule. A repeat of the same
/// artist/title/album within one play is a seek/restart, not a new listen.
inline bool sameTrackIdentity(const ScrobbleTrack& a, const ScrobbleTrack& b) {
    return a.artist == b.artist && a.title == b.title && a.album == b.album;
}

/// Dedup key for the offline queue: stable across restarts, service-specific
/// (Last.fm and ListenBrainz are separate submissions), and contains nothing
/// secret.
inline std::string listenDedupKey(ListenService service, const ScrobbleTrack& track,
                                  std::int64_t timestampUnix) {
    std::string key = service == ListenService::LastFm ? "lastfm" : "listenbrainz";
    key.push_back('\x1f');
    key += track.artist;
    key.push_back('\x1f');
    key += track.title;
    key.push_back('\x1f');
    key += std::to_string(timestampUnix);
    return key;
}

inline std::string_view listenServiceName(ListenService service) {
    return service == ListenService::LastFm ? "last.fm" : "listenbrainz";
}

} // namespace koalaamp::scrobble
