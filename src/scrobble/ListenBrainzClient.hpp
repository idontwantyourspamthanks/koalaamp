// SPDX-License-Identifier: MIT
// scrobble — ListenBrainz submit-listens client.
//
// Wire contract verified against the public docs, 2026-10:
//   https://listenbrainz.readthedocs.io/en/latest/users/api/core.html
//     POST /1/submit-listens, Authorization: Token <user token>,
//     Content-Type: application/json; 200 accepted, 400 bad JSON, 401 bad token.
//   https://listenbrainz.readthedocs.io/en/latest/users/json.html
//     `{"listen_type":"single","payload":[{"listened_at":…,"track_metadata":{…}}]}`;
//     `playing_now` has no `listened_at`; MBIDs live in
//     `track_metadata.additional_info` (`recording_mbid`, `release_mbid`,
//     `artist_mbids`). The 50 %/4-minute submission rule is the same as Last.fm's,
//     so ScrobbleCoordinator owns it for both services.
//
// The token is sent only in the Authorization header: never in the URL, never in
// the payload, never in the persisted queue.

#pragma once

#include "scrobble/ScrobbleSubmitter.hpp"
#include "scrobble/Transport.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace koalaamp::scrobble {

struct ListenBrainzError {
    long httpStatus{0};
    std::string message;
    bool transport{false};

    bool ok() const { return !transport && message.empty() && httpStatus >= 200 &&
                             httpStatus < 300; }
};

class ListenBrainzClient final : public IListenBrainzSubmitter {
public:
    ListenBrainzClient(std::string token, ITransport& transport,
                       std::string baseUrl = "https://api.listenbrainz.org");

    bool submitPlayingNow(const ScrobbleTrack& track) override;
    bool submitSingle(const ScrobbleTrack& track, std::int64_t listenedAtUnix) override;

    const ListenBrainzError& lastError() const { return lastError_; }

private:
    bool submit(std::string_view listenType, const ScrobbleTrack& track,
                const std::int64_t* listenedAtUnix);

    std::string token_;
    ITransport& transport_;
    std::string baseUrl_;
    ListenBrainzError lastError_;
};

} // namespace koalaamp::scrobble
