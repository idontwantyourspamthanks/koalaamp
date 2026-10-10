// SPDX-License-Identifier: MIT
// servers — Jellyfin/Plex source gateways (docs/spec.md §2.3, §8).
// Interface contract stub for FE-0; ports land with SV-1/SV-2/SV-4.

#pragma once

#include "library/ITrackSource.hpp"

#include <string>

namespace koalaamp::servers {

/// Server connection state. The token is held in memory only and never
/// persisted to the repository (see AGENTS.md).
struct ServerAuth {
    std::string baseUrl;
    std::string userId;
    std::string accessToken;
    std::string deviceId;
};

/// A remote track source: browse, search, stream and rate against a server.
/// Authentication/refresh policy is per-backend (Jellyfin vs Plex).
class IServerSource : public library::ITrackSource {
public:
    ~IServerSource() override = default;

    virtual bool isAuthenticated() const = 0;
    virtual void signOut() = 0;
};

} // namespace koalaamp::servers
