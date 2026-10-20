// SPDX-License-Identifier: MIT
// ui — Jellyfin adapter. See JellyfinBrowserSource.hpp.

#include "ui/JellyfinBrowserSource.hpp"

#include "servers/jellyfin/JellyfinClient.hpp"

#include <utility>
#include <variant>

namespace koalaamp::ui {
namespace {

template <typename Outcome>
void describe(const Outcome& outcome, std::string& error, bool& unauthorized) {
    if (std::holds_alternative<koalaamp::servers::jellyfin::ItemsUnauthorized>(outcome)) {
        unauthorized = true;
        error = "The server rejected the token";
    } else if (std::holds_alternative<koalaamp::servers::jellyfin::ItemsUnreachable>(outcome)) {
        error = "Server unreachable";
    } else {
        error = std::get<koalaamp::servers::jellyfin::ItemsFailed>(outcome).message;
    }
}

} // namespace

JellyfinBrowserSource::JellyfinBrowserSource(servers::jellyfin::IHttpTransport& transport,
                                             servers::jellyfin::DeviceProfile profile,
                                             servers::ServerAuth session)
    : session_(std::move(session)), client_(transport, std::move(profile)) {}

void JellyfinBrowserSource::reset() {
    error_.clear();
    unauthorized_ = false;
}

std::vector<BrowserItem> JellyfinBrowserSource::libraries() {
    reset();
    using namespace koalaamp::servers::jellyfin;
    const LibrariesOutcome outcome = client_.musicLibraries(session_.baseUrl, session_.accessToken,
                                                            session_.deviceId, session_.userId);
    std::vector<BrowserItem> items;
    if (const auto* success = std::get_if<LibrariesSuccess>(&outcome)) {
        for (const MusicLibrary& library : success->libraries) {
            if (library.collectionType == "music") {
                items.push_back(BrowserItem{library.id, library.name});
            }
        }
        return items;
    }
    describe(outcome, error_, unauthorized_);
    return items;
}

std::vector<BrowserItem> JellyfinBrowserSource::albums(const std::string& libraryId) {
    reset();
    using namespace koalaamp::servers::jellyfin;
    const ItemsOutcome outcome = client_.musicAlbumsWindow(session_.baseUrl, session_.accessToken,
                                                           session_.deviceId, libraryId, 0, 500);
    std::vector<BrowserItem> items;
    if (const auto* success = std::get_if<ItemsSuccess>(&outcome)) {
        for (const MediaItem& album : success->window.items) {
            items.push_back(BrowserItem{album.id, album.name});
        }
        return items;
    }
    describe(outcome, error_, unauthorized_);
    return items;
}

std::vector<library::Track> JellyfinBrowserSource::albumTracks(const std::string& albumId) {
    reset();
    using namespace koalaamp::servers::jellyfin;
    const TracksOutcome outcome =
        client_.albumTracks(session_.baseUrl, session_.accessToken, session_.deviceId, albumId);
    if (const auto* success = std::get_if<TracksSuccess>(&outcome)) {
        return success->tracks;
    }
    describe(outcome, error_, unauthorized_);
    return {};
}

std::vector<BrowserItem> JellyfinBrowserSource::playlists() {
    reset();
    using namespace koalaamp::servers::jellyfin;
    const NamedItemsOutcome outcome =
        client_.playlists(session_.baseUrl, session_.accessToken, session_.deviceId);
    std::vector<BrowserItem> items;
    if (const auto* success = std::get_if<NamedItemsSuccess>(&outcome)) {
        for (const NamedItem& playlist : success->items) {
            items.push_back(BrowserItem{playlist.id, playlist.name});
        }
        return items;
    }
    describe(outcome, error_, unauthorized_);
    return items;
}

std::vector<library::Track> JellyfinBrowserSource::playlistItems(const std::string& playlistId) {
    reset();
    using namespace koalaamp::servers::jellyfin;
    const TracksOutcome outcome = client_.playlistItems(
        session_.baseUrl, session_.accessToken, session_.deviceId, playlistId, session_.userId);
    if (const auto* success = std::get_if<TracksSuccess>(&outcome)) {
        return success->tracks;
    }
    describe(outcome, error_, unauthorized_);
    return {};
}

} // namespace koalaamp::ui
