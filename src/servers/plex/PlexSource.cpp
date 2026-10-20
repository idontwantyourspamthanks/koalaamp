// SPDX-License-Identifier: MIT
// plex — IServerSource. See PlexSource.hpp.

#include "servers/plex/PlexSource.hpp"

#include "servers/plex/PlexStream.hpp"

#include <utility>

namespace koalaamp::servers::plex {
namespace {

std::string baseNoSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

} // namespace

PlexSource::PlexSource(jellyfin::IHttpTransport& transport, PlexClientInfo info,
                       PlexSession session)
    : transport_(transport), info_(std::move(info)), session_(std::move(session)) {
    if (isAuthenticated()) {
        client_.emplace(transport_, info_, session_.baseUrl, session_.token);
    }
}

bool PlexSource::isAuthenticated() const {
    return !session_.baseUrl.empty() && !session_.token.empty();
}

void PlexSource::setSession(PlexSession session) {
    session_ = std::move(session);
    lastError_.clear();
    if (isAuthenticated()) {
        client_.emplace(transport_, info_, session_.baseUrl, session_.token);
    } else {
        client_.reset();
    }
}

void PlexSource::signOut() {
    session_.token.clear();
    session_.userId.clear();
    session_.userName.clear();
    client_.reset();
    lastError_.clear();
}

std::vector<library::Track> PlexSource::search(const library::Query& query,
                                               library::Cancellation& cancel) {
    if (!client_) {
        lastError_ = "Not signed in";
        return {};
    }
    const int limit = query.limit > 0 ? query.limit : 100;
    const auto tracks = client_->search(query.text, limit, cancel);
    if (!tracks) {
        lastError_ = client_->lastError();
        return {};
    }
    lastError_.clear();
    return *tracks;
}

std::unique_ptr<std::istream> PlexSource::openStream(const library::Track& track,
                                                     library::StreamIntent /*intent*/) {
    if (!isAuthenticated()) {
        lastError_ = "Not signed in";
        return nullptr;
    }
    if (track.path.empty()) {
        lastError_ = "Track has no media part";
        return nullptr;
    }
    std::string error;
    std::unique_ptr<std::istream> stream =
        openPlexStream(transport_, baseNoSlash(session_.baseUrl) + track.path,
                       plexHeaders(info_, session_.token), &error);
    if (!stream) {
        lastError_ = error.empty() ? "Could not open the media part" : error;
    } else {
        lastError_.clear();
    }
    return stream;
}

void PlexSource::setRating(const library::Track& track, int stars) {
    if (!client_) {
        lastError_ = "Not signed in";
        return;
    }
    if (client_->rate(track.id, stars)) {
        lastError_.clear();
    } else {
        lastError_ = client_->lastError();
    }
}

} // namespace koalaamp::servers::plex
