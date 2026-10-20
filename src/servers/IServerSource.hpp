// SPDX-License-Identifier: MIT
// servers — Jellyfin/Plex source gateways (docs/spec.md §2.3, §8).
// Interface contract stub for FE-0; ports land with SV-1/SV-2/SV-4.

#pragma once

#include "library/ITrackSource.hpp"

#include <string>

namespace koalaamp::servers {

enum class ServerBackend { Jellyfin, Plex };

/// Server connection state. The access token is not written to the server book
/// or the repository. A SecretStore may keep it in the OS keychain between runs.
struct ServerAuth {
    std::string baseUrl;
    std::string userId;
    std::string accessToken;
    std::string deviceId;
    /// Jellyfin `ServerId` when the server sent one. Empty for callers that only
    /// have a base URL; the keychain key falls back to `baseUrl` in that case.
    std::string serverId;
    /// Which backend issued the session (Jellyfin gateway vs Plex PMS).
    ServerBackend backend{ServerBackend::Jellyfin};
    /// Display name of the account (Plex's account title; Jellyfin's user name).
    std::string userName;
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
