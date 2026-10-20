// SPDX-License-Identifier: MIT
// plex — PMS browse client (SV-3).
//
// Ported from koalapaw `PlexClient.kt` / `PlexBrowse.kt`, narrowed to music:
// sections → albums → tracks, `/hubs/search`, artwork and `/:/rate`. The token
// rides X-Plex-Token on every request; URLs never carry it.

#pragma once

#include "library/ITrackSource.hpp"
#include "servers/jellyfin/HttpTransport.hpp"
#include "servers/plex/PlexTypes.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace koalaamp::servers::plex {

class PlexClient {
public:
    PlexClient(jellyfin::IHttpTransport& transport, PlexClientInfo info, std::string baseUrl,
               std::string token);

    std::optional<PlexServerInfo> identity(
        const library::Cancellation& cancel = jellyfin::NoCancellation{});
    /// `/library/sections`; callers filter `type == "artist"` for music.
    std::optional<std::vector<PlexSection>> sections(
        const library::Cancellation& cancel = jellyfin::NoCancellation{});
    /// Albums in a music section: `/library/sections/{id}/all?type=9` with paging.
    std::optional<std::vector<PlexAlbum>> albums(
        const std::string& sectionId, int start, int limit,
        const library::Cancellation& cancel = jellyfin::NoCancellation{});
    /// Album tracks: `/library/metadata/{ratingKey}/children`.
    std::optional<std::vector<library::Track>> albumTracks(
        const std::string& albumId,
        const library::Cancellation& cancel = jellyfin::NoCancellation{});
    /// Global track search: `/hubs/search`, flattened across hubs.
    std::optional<std::vector<library::Track>> search(
        std::string_view query, int limit,
        const library::Cancellation& cancel = jellyfin::NoCancellation{});
    /// Fetch original artwork or a sized transcode; bytes returned, token header.
    std::optional<std::string> artwork(const std::string& thumbPath, int width, int height,
                                       const library::Cancellation& cancel =
                                           jellyfin::NoCancellation{});
    /// `PUT /:/rate` — 4 stars = 8, 0 stars = −1 (unset).
    bool rate(const std::string& ratingKey, int stars,
              const library::Cancellation& cancel = jellyfin::NoCancellation{});

    const std::string& lastError() const { return lastError_; }

    /// One `Metadata` DTO → the shared track model (also used by tests via the
    /// public browse methods).
    static library::Track trackFromMetadata(const std::string& dtoJson);

private:
    jellyfin::HttpResponse get(std::string_view path,
                               const std::vector<std::pair<std::string, std::string>>& params,
                               bool includeToken, const library::Cancellation& cancel);
    jellyfin::HttpResponse put(std::string_view path,
                               const std::vector<std::pair<std::string, std::string>>& params,
                               const library::Cancellation& cancel);
    bool ensureOk(const jellyfin::HttpResponse& response, const char* what);

    jellyfin::IHttpTransport& transport_;
    PlexClientInfo info_;
    std::string baseUrl_;
    std::string token_;
    std::string lastError_;
};

} // namespace koalaamp::servers::plex
