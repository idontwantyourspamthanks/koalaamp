// SPDX-License-Identifier: MIT
// ui — the browse surface the Server Browser window needs, backend-neutral.
//
// Jellyfin and Plex have different gateway shapes; the window only ever wants
// "libraries → albums → tracks" plus read-only playlists. Adapters live beside
// this header (JellyfinBrowserSource / PlexBrowserSource) and own their clients.

#pragma once

#include "library/ITrackSource.hpp"

#include <string>
#include <vector>

namespace koalaamp::ui {

struct BrowserItem {
    std::string id;
    std::string name;
};

class IServerBrowserSource {
public:
    virtual ~IServerBrowserSource() = default;

    virtual std::vector<BrowserItem> libraries() = 0;
    virtual std::vector<BrowserItem> albums(const std::string& libraryId) = 0;
    virtual std::vector<library::Track> albumTracks(const std::string& albumId) = 0;
    /// Read-only playlists; a backend without them returns empty.
    virtual std::vector<BrowserItem> playlists() = 0;
    virtual std::vector<library::Track> playlistItems(const std::string& playlistId) = 0;

    /// Why the last call returned nothing (empty on success).
    virtual const std::string& error() const = 0;
    /// True when the last failure was a 401 (the window closes/re-auths).
    virtual bool unauthorized() const = 0;
};

} // namespace koalaamp::ui
