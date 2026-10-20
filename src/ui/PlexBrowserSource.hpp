// SPDX-License-Identifier: MIT
// ui — Plex adapter for the Server Browser window.

#pragma once

#include "servers/jellyfin/HttpTransport.hpp"
#include "servers/plex/PlexClient.hpp"
#include "servers/plex/PlexTypes.hpp"
#include "ui/ServerBrowserSource.hpp"

#include <string>
#include <vector>

namespace koalaamp::ui {

class PlexBrowserSource final : public IServerBrowserSource {
public:
    PlexBrowserSource(servers::jellyfin::IHttpTransport& transport,
                      servers::plex::PlexClientInfo info, servers::plex::PlexSession session);

    std::vector<BrowserItem> libraries() override;
    std::vector<BrowserItem> albums(const std::string& libraryId) override;
    std::vector<library::Track> albumTracks(const std::string& albumId) override;
    std::vector<BrowserItem> playlists() override;
    std::vector<library::Track> playlistItems(const std::string& playlistId) override;

    const std::string& error() const override { return error_; }
    bool unauthorized() const override { return unauthorized_; }

private:
    void reset();
    void fail();

    servers::plex::PlexSession session_;
    servers::plex::PlexClient client_;
    std::string error_;
    bool unauthorized_{false};
};

} // namespace koalaamp::ui
