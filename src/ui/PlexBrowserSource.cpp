// SPDX-License-Identifier: MIT
// ui — Plex adapter. See PlexBrowserSource.hpp.

#include "ui/PlexBrowserSource.hpp"

#include <utility>

namespace koalaamp::ui {

PlexBrowserSource::PlexBrowserSource(servers::jellyfin::IHttpTransport& transport,
                                     servers::plex::PlexClientInfo info,
                                     servers::plex::PlexSession session)
    : session_(std::move(session)),
      client_(transport, std::move(info), session_.baseUrl, session_.token) {}

void PlexBrowserSource::reset() {
    error_.clear();
    unauthorized_ = false;
}

void PlexBrowserSource::fail() {
    error_ = client_.lastError();
    unauthorized_ = client_.lastUnauthorized();
}

std::vector<BrowserItem> PlexBrowserSource::libraries() {
    reset();
    std::vector<BrowserItem> items;
    const auto sections = client_.sections();
    if (!sections) {
        fail();
        return items;
    }
    for (const auto& section : *sections) {
        if (section.type == "artist") { // Plex's music section kind
            items.push_back(BrowserItem{section.id, section.title});
        }
    }
    return items;
}

std::vector<BrowserItem> PlexBrowserSource::albums(const std::string& libraryId) {
    reset();
    std::vector<BrowserItem> items;
    const auto albums = client_.albums(libraryId, 0, 500);
    if (!albums) {
        fail();
        return items;
    }
    for (const auto& album : *albums) {
        items.push_back(BrowserItem{album.id, album.title});
    }
    return items;
}

std::vector<library::Track> PlexBrowserSource::albumTracks(const std::string& albumId) {
    reset();
    const auto tracks = client_.albumTracks(albumId);
    if (!tracks) {
        fail();
        return {};
    }
    return *tracks;
}

std::vector<BrowserItem> PlexBrowserSource::playlists() {
    reset();
    return {}; // Plex playlist browsing is not part of SV-3/SV-4.
}

std::vector<library::Track> PlexBrowserSource::playlistItems(const std::string& /*playlistId*/) {
    reset();
    return {};
}

} // namespace koalaamp::ui
