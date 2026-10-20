// SPDX-License-Identifier: MIT
// scrobble — asynchronous scrobble worker (SC-1 app wiring).
//
// The policy/coordinator is deliberately synchronous, but network calls must not
// run on PlaybackCore's dispatch thread (or the UI thread). This wrapper owns a
// worker thread and applies start/progress/end/mode/config commands in order, so
// now-playing and submissions can block on HTTP without stalling playback.

#pragma once

#include "scrobble/LastFmClient.hpp"
#include "scrobble/ScrobbleCoordinator.hpp"
#include "scrobble/Transport.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace koalaamp::scrobble {

class ScrobbleService {
public:
    /// `queueFile` is the offline retry queue. Mode defaults to Off. The Last.fm
    /// session and ListenBrainz token are empty when the user has not signed in.
    ScrobbleService(ITransport& transport, std::filesystem::path queueFile, ScrobbleMode mode,
                    LastFmCredentials lastFm, std::string lastFmSessionKey,
                    std::string listenBrainzToken);
    ~ScrobbleService();

    ScrobbleService(const ScrobbleService&) = delete;
    ScrobbleService& operator=(const ScrobbleService&) = delete;

    void setMode(ScrobbleMode mode);
    void setLastFm(LastFmCredentials credentials, std::string sessionKey);
    void setListenBrainz(std::string token);

    void onTrackStarted(ScrobbleTrack track, std::int64_t startedAtUnix);
    void onProgress(std::int64_t positionMs, std::int64_t lengthMs);
    void onTrackEnded();
    void onStopped();
    void flushPending();

    std::size_t pendingCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace koalaamp::scrobble
