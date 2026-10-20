// SPDX-License-Identifier: MIT
// jellyfin — IServerSource over the SV-1 gateway. See JellyfinSource.hpp.

#include "servers/jellyfin/JellyfinSource.hpp"

#include "servers/jellyfin/JellyfinStream.hpp"

#include <utility>
#include <variant>

namespace koalaamp::servers::jellyfin {
namespace {

std::string describe(const ReportOutcome& outcome) {
    if (std::holds_alternative<ItemsUnauthorized>(outcome)) {
        return "The server rejected the token";
    }
    if (std::holds_alternative<ItemsUnreachable>(outcome)) {
        return "Server unreachable";
    }
    return std::get<ItemsFailed>(outcome).message;
}

} // namespace

JellyfinSource::JellyfinSource(IHttpTransport& transport, DeviceProfile profile,
                               koalaamp::servers::ServerAuth auth)
    : transport_(transport),
      profile_(std::move(profile)),
      auth_(std::move(auth)),
      client_(transport_, profile_) {}

bool JellyfinSource::isAuthenticated() const {
    return !auth_.baseUrl.empty() && !auth_.accessToken.empty();
}

void JellyfinSource::signOut() {
    auth_.accessToken.clear();
    auth_.userId.clear();
    lastError_.clear();
}

void JellyfinSource::setSession(koalaamp::servers::ServerAuth auth) {
    auth_ = std::move(auth);
    lastError_.clear();
}

std::vector<library::Track> JellyfinSource::search(const library::Query& query,
                                                   library::Cancellation& cancel) {
    if (!isAuthenticated()) {
        lastError_ = "Not signed in";
        return {};
    }
    const int limit = query.limit > 0 ? query.limit : 100;
    const SearchOutcome outcome =
        client_.search(auth_.baseUrl, auth_.accessToken, auth_.deviceId, libraryId_, query.text,
                       "Audio", limit, cancel);
    if (const auto* success = std::get_if<SearchSuccess>(&outcome)) {
        lastError_.clear();
        return success->tracks;
    }
    if (std::holds_alternative<ItemsUnauthorized>(outcome)) {
        lastError_ = "The server rejected the token";
    } else if (std::holds_alternative<ItemsUnreachable>(outcome)) {
        lastError_ = "Server unreachable";
    } else {
        lastError_ = std::get<ItemsFailed>(outcome).message;
    }
    return {};
}

std::unique_ptr<std::istream> JellyfinSource::openStream(const library::Track& track,
                                                         library::StreamIntent /*intent*/) {
    if (!isAuthenticated()) {
        lastError_ = "Not signed in";
        return nullptr;
    }
    // Direct playback and download both stream the original file; downloads are
    // LI-*'s concern, so the intent does not change the wire request here.
    std::string error;
    std::unique_ptr<std::istream> stream =
        openJellyfinStream(transport_, profile_, auth_.baseUrl, auth_.accessToken, auth_.deviceId,
                           StreamSpec{track.id}, &error);
    if (!stream) {
        lastError_ = error.empty() ? "Could not open the stream" : error;
    } else {
        lastError_.clear();
    }
    return stream;
}

void JellyfinSource::setRating(const library::Track& track, int stars) {
    if (!isAuthenticated()) {
        lastError_ = "Not signed in";
        return;
    }
    const ReportOutcome outcome = client_.setRating(
        auth_.baseUrl, auth_.accessToken, auth_.deviceId, auth_.userId, track.id, stars);
    if (std::holds_alternative<ReportSuccess>(outcome)) {
        lastError_.clear();
    } else {
        lastError_ = describe(outcome);
    }
}

} // namespace koalaamp::servers::jellyfin
