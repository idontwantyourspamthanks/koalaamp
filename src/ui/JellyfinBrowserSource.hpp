// SPDX-License-Identifier: MIT
// ui — Jellyfin adapter for the Server Browser window.

#pragma once

#include "servers/IServerSource.hpp"
#include "servers/jellyfin/AuthHeaders.hpp"
#include "servers/jellyfin/HttpTransport.hpp"
#include "servers/jellyfin/JellyfinClient.hpp"
#include "ui/ServerBrowserSource.hpp"

#include <string>
#include <vector>

namespace koalaamp::ui {

class JellyfinBrowserSource final : public IServerBrowserSource {
public:
    JellyfinBrowserSource(servers::jellyfin::IHttpTransport& transport,
                          servers::jellyfin::DeviceProfile profile, servers::ServerAuth session);

    std::vector<BrowserItem> libraries() override;
    std::vector<BrowserItem> albums(const std::string& libraryId) override;
    std::vector<library::Track> albumTracks(const std::string& albumId) override;
    std::vector<BrowserItem> playlists() override;
    std::vector<library::Track> playlistItems(const std::string& playlistId) override;

    const std::string& error() const override { return error_; }
    bool unauthorized() const override { return unauthorized_; }

private:
    void reset();

    servers::ServerAuth session_;
    servers::jellyfin::JellyfinClient client_;
    std::string error_;
    bool unauthorized_{false};
};

} // namespace koalaamp::ui
