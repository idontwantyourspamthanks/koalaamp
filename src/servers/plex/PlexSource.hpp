// SPDX-License-Identifier: MIT
// plex — IServerSource over the PMS client (SV-3).
//
// Search, direct-part streaming and rating. Playback/progress reporting is SV-4;
// this source only needs to hand the playback layer a stream and a track.

#pragma once

#include "servers/IServerSource.hpp"
#include "servers/plex/PlexClient.hpp"
#include "servers/plex/PlexTypes.hpp"

#include <memory>
#include <optional>
#include <string>

namespace koalaamp::servers::plex {

class PlexSource final : public koalaamp::servers::IServerSource {
public:
    PlexSource(jellyfin::IHttpTransport& transport, PlexClientInfo info, PlexSession session);

    // --- ITrackSource --------------------------------------------------------
    std::vector<library::Track> search(const library::Query& query,
                                       library::Cancellation& cancel) override;
    std::unique_ptr<std::istream> openStream(const library::Track& track,
                                             library::StreamIntent intent) override;
    void setRating(const library::Track& track, int stars) override;

    // --- IServerSource -------------------------------------------------------
    bool isAuthenticated() const override;
    void signOut() override;

    void setSession(PlexSession session);
    const PlexSession& session() const { return session_; }
    const std::string& lastError() const { return lastError_; }

private:
    jellyfin::IHttpTransport& transport_;
    PlexClientInfo info_;
    PlexSession session_;
    std::optional<PlexClient> client_;
    std::string lastError_;
};

} // namespace koalaamp::servers::plex
