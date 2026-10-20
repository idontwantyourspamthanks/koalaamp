// SPDX-License-Identifier: MIT
// plex — plex.tv PIN/link authentication and resource listing (SV-3).
//
// Ported from koalapaw `PlexAuth.kt`: without `strong` plex.tv issues the short
// 4-character code TV clients show; the account token from the link flow is
// exchanged through `/api/v2/resources` for each server's PMS-scoped token (a
// kid/borrowed account token 401s the PMS directly).

#pragma once

#include "servers/jellyfin/HttpTransport.hpp"
#include "servers/plex/PlexTypes.hpp"

#include <string>
#include <variant>
#include <vector>

namespace koalaamp::servers::plex {

struct PlexAuthUnreachable {};
struct PlexAuthFailed {
    std::string message;
};

using PlexPinOutcome = std::variant<PlexPin, PlexAuthUnreachable, PlexAuthFailed>;

struct PlexLinkPending {};
struct PlexLinkExpired {};
using PlexPollOutcome = std::variant<PlexLinkPending, PlexLinkExpired, PlexAccount,
                                     PlexAuthUnreachable, PlexAuthFailed>;

using PlexResourcesOutcome =
    std::variant<std::vector<PlexResource>, PlexAuthUnreachable, PlexAuthFailed>;

class PlexAuth {
public:
    PlexAuth(jellyfin::IHttpTransport& transport, PlexClientInfo info,
             std::string plexTvBase = "https://plex.tv");

    /// `POST /api/v2/pins` — the code the user types at the returned link URL.
    PlexPinOutcome createPin(const library::Cancellation& cancel = jellyfin::NoCancellation{});

    /// `GET /api/v2/pins/{id}`: pending until the user confirms; then the account
    /// token is resolved through `/api/v2/user`.
    PlexPollOutcome pollPin(const std::string& pinId,
                            const library::Cancellation& cancel = jellyfin::NoCancellation{});

    /// `GET /api/v2/resources?includeHttps=1&includeRelay=1` with the account token.
    PlexResourcesOutcome resources(const std::string& accountToken,
                                   const library::Cancellation& cancel =
                                       jellyfin::NoCancellation{});

    /// The page the user opens to confirm the code (`https://plex.tv/link`).
    static std::string linkUrl();

    const std::string& lastError() const { return lastError_; }

private:
    jellyfin::HttpResponse get(const std::string& url, const std::string& token,
                               const library::Cancellation& cancel);
    jellyfin::HttpResponse post(const std::string& url,
                                const std::vector<std::pair<std::string, std::string>>& headers,
                                const std::string& body,
                                const library::Cancellation& cancel);

    jellyfin::IHttpTransport& transport_;
    PlexClientInfo info_;
    std::string plexTvBase_;
    std::string lastError_;
};

} // namespace koalaamp::servers::plex
