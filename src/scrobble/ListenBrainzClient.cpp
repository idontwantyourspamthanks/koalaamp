// SPDX-License-Identifier: MIT
// scrobble — ListenBrainz submit-listens client. See ListenBrainzClient.hpp.

#include "scrobble/ListenBrainzClient.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace koalaamp::scrobble {
namespace {

using json = nlohmann::json;

std::string withoutTrailingSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

} // namespace

ListenBrainzClient::ListenBrainzClient(std::string token, ITransport& transport,
                                       std::string baseUrl)
    : token_(std::move(token)), transport_(transport), baseUrl_(withoutTrailingSlash(std::move(baseUrl))) {}

bool ListenBrainzClient::submitPlayingNow(const ScrobbleTrack& track) {
    return submit("playing_now", track, nullptr);
}

bool ListenBrainzClient::submitSingle(const ScrobbleTrack& track, std::int64_t listenedAtUnix) {
    return submit("single", track, &listenedAtUnix);
}

bool ListenBrainzClient::submit(std::string_view listenType, const ScrobbleTrack& track,
                                const std::int64_t* listenedAtUnix) {
    lastError_ = {};
    if (!track.valid()) {
        lastError_.message = "a listen needs an artist and a track name";
        return false;
    }
    if (token_.empty()) {
        lastError_.message = "no ListenBrainz user token";
        return false;
    }

    json metadata = json::object();
    metadata["artist_name"] = track.artist;
    metadata["track_name"] = track.title;
    if (!track.album.empty()) {
        metadata["release_name"] = track.album;
    }

    json additional = json::object();
    if (!track.recordingMbid.empty()) {
        additional["recording_mbid"] = track.recordingMbid;
    }
    if (!track.releaseMbid.empty()) {
        additional["release_mbid"] = track.releaseMbid;
    }
    if (!track.artistMbids.empty()) {
        additional["artist_mbids"] = track.artistMbids;
    }
    additional["submission_client"] = "KoalaAmp";
    metadata["additional_info"] = std::move(additional);

    json listen = json::object();
    if (listenedAtUnix != nullptr) {
        listen["listened_at"] = *listenedAtUnix;
    }
    listen["track_metadata"] = std::move(metadata);

    json payload = json::object();
    payload["listen_type"] = listenType;
    payload["payload"] = json::array({std::move(listen)});

    HttpRequest request;
    request.method = "POST";
    request.url = baseUrl_ + "/1/submit-listens";
    request.headers = {
        {"Authorization", "Token " + token_},
        {"Content-Type", "application/json"},
        {"Accept", "application/json"},
    };
    request.body = payload.dump();

    const HttpResponse response = transport_.perform(request);
    lastError_.httpStatus = response.status;
    if (!response.transportOk()) {
        lastError_.transport = true;
        lastError_.message = response.error;
        return false;
    }
    if (!response.httpOk()) {
        try {
            const json root = json::parse(response.body);
            if (root.is_object()) {
                const auto errorIt = root.find("error");
                if (errorIt != root.end() && errorIt->is_string()) {
                    lastError_.message = errorIt->get<std::string>();
                } else {
                    const auto messageIt = root.find("message");
                    if (messageIt != root.end() && messageIt->is_string()) {
                        lastError_.message = messageIt->get<std::string>();
                    }
                }
            }
        } catch (const json::exception&) {
            // Fall through to the status message.
        }
        if (lastError_.message.empty()) {
            lastError_.message = "ListenBrainz HTTP " + std::to_string(response.status);
        }
        return false;
    }
    return true;
}

} // namespace koalaamp::scrobble
