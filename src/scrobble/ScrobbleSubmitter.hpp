// SPDX-License-Identifier: MIT
// scrobble — the submitter seams the coordinator consumes.
//
// Deliberately narrower than LastFmClient / ListenBrainzClient so the policy
// tests can use fakes and the coordinator never links PlaybackCore (SC-1's
// dependency on SV-2 is conceptual: it consumes explicit start/progress/end
// calls, not the playback engine).

#pragma once

#include "scrobble/ScrobbleTrack.hpp"

#include <cstdint>
#include <vector>

namespace koalaamp::scrobble {

class ILastFmSubmitter {
public:
    virtual ~ILastFmSubmitter() = default;
    virtual bool updateNowPlaying(const ScrobbleTrack& track) = 0;
    /// `batch` is at most 50 entries (Last.fm's documented limit).
    virtual bool scrobble(const std::vector<TimedScrobble>& batch) = 0;
};

class IListenBrainzSubmitter {
public:
    virtual ~IListenBrainzSubmitter() = default;
    virtual bool submitPlayingNow(const ScrobbleTrack& track) = 0;
    virtual bool submitSingle(const ScrobbleTrack& track, std::int64_t listenedAtUnix) = 0;
};

} // namespace koalaamp::scrobble
