// SPDX-License-Identifier: MIT
// scrobble — Last.fm API 2.0 client. See LastFmClient.hpp.

#include "scrobble/LastFmClient.hpp"

#include "scrobble/Md5.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <utility>

namespace koalaamp::scrobble {
namespace {

using json = nlohmann::json;
using Params = std::vector<std::pair<std::string, std::string>>;

/// RFC 3986 unreserved set left alone; everything else percent-encoded with
/// uppercase hex. Used for the POST body (spaces as %20, accepted by Last.fm).
std::string encodeForm(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const unsigned char c : value) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                                c == '~';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0f]);
        }
    }
    return out;
}

std::string indexed(const char* name, int index) {
    if (index < 0) {
        return name;
    }
    return std::string(name) + "[" + std::to_string(index) + "]";
}

void appendTrackParams(Params& params, const ScrobbleTrack& track, int index,
                       std::int64_t timestamp, bool withTimestamp) {
    params.emplace_back(indexed("artist", index), track.artist);
    params.emplace_back(indexed("track", index), track.title);
    if (!track.album.empty()) {
        params.emplace_back(indexed("album", index), track.album);
    }
    if (!track.albumArtist.empty()) {
        params.emplace_back(indexed("albumArtist", index), track.albumArtist);
    }
    if (track.durationMs > 0) {
        params.emplace_back(indexed("duration", index), std::to_string(track.durationMs / 1000));
    }
    if (track.trackNumber > 0) {
        params.emplace_back(indexed("trackNumber", index), std::to_string(track.trackNumber));
    }
    if (!track.recordingMbid.empty()) {
        params.emplace_back(indexed("mbid", index), track.recordingMbid);
    }
    if (withTimestamp) {
        params.emplace_back(indexed("timestamp", index), std::to_string(timestamp));
    }
}

int jsonInt(const json& value) {
    if (value.is_number_integer()) {
        return value.get<int>();
    }
    if (value.is_string()) {
        try {
            return std::stoi(value.get<std::string>());
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

std::string jsonString(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

/// Parses Last.fm's `{"error":N,"message":"…"}` first: it is what a rejected
/// signature or an expired session actually returns, usually with HTTP 4xx.
bool decodeResponse(const HttpResponse& response, json& root, LastFmError& error) {
    error = {};
    error.httpStatus = response.status;
    if (!response.transportOk()) {
        error.transport = true;
        error.message = response.error;
        return false;
    }
    try {
        root = json::parse(response.body);
    } catch (const json::exception&) {
        error.message = "Last.fm returned malformed JSON (HTTP " +
                        std::to_string(response.status) + ")";
        return false;
    }
    if (root.is_object() && root.contains("error")) {
        error.code = jsonInt(root["error"]);
        error.message = jsonString(root, "message");
        if (error.message.empty()) {
            error.message = "Last.fm error " + std::to_string(error.code);
        }
        return false;
    }
    if (!response.httpOk()) {
        error.message = "Last.fm HTTP " + std::to_string(response.status);
        return false;
    }
    return true;
}

} // namespace

std::string lastFmAuthUrl(std::string_view apiKey, std::string_view requestToken) {
    return "https://www.last.fm/api/auth/?api_key=" + encodeForm(apiKey) +
           "&token=" + encodeForm(requestToken);
}

LastFmClient::LastFmClient(LastFmCredentials credentials, ITransport& transport,
                           std::string baseUrl)
    : credentials_(std::move(credentials)), transport_(transport), baseUrl_(std::move(baseUrl)) {}

void LastFmClient::setSessionKey(std::string sessionKey) {
    sessionKey_ = std::move(sessionKey);
}

void LastFmClient::clearError() { lastError_ = {}; }

std::string LastFmClient::sign(Params params) const {
    // The docs: sort all parameters by name, concatenate name+value, append the
    // shared secret, MD5. `format` and `callback` are excluded from the input.
    params.erase(std::remove_if(params.begin(), params.end(),
                                [](const auto& entry) {
                                    return entry.first == "format" || entry.first == "callback";
                                }),
                 params.end());
    std::sort(params.begin(), params.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string joined;
    for (const auto& [name, value] : params) {
        joined += name;
        joined += value;
    }
    joined += credentials_.sharedSecret;
    return md5Hex(joined);
}

HttpResponse LastFmClient::post(Params params) {
    const std::string signature = sign(params);
    params.emplace_back("format", "json");
    params.emplace_back("api_sig", signature);

    std::string body;
    for (const auto& [name, value] : params) {
        if (!body.empty()) {
            body.push_back('&');
        }
        body += encodeForm(name);
        body.push_back('=');
        body += encodeForm(value);
    }

    HttpRequest request;
    request.method = "POST";
    request.url = baseUrl_;
    request.headers = {
        {"Content-Type", "application/x-www-form-urlencoded"},
        {"Accept", "application/json"},
    };
    request.body = std::move(body);
    return transport_.perform(request);
}

std::optional<std::string> LastFmClient::getToken() {
    clearError();
    Params params;
    params.emplace_back("method", "auth.getToken");
    params.emplace_back("api_key", credentials_.apiKey);

    const HttpResponse response = post(std::move(params));
    json root;
    if (!decodeResponse(response, root, lastError_)) {
        return std::nullopt;
    }
    const std::string token = jsonString(root, "token");
    if (token.empty()) {
        lastError_.message = "auth.getToken response carried no token";
        return std::nullopt;
    }
    return token;
}

std::optional<LastFmSession> LastFmClient::getSession(std::string_view token) {
    clearError();
    if (token.empty()) {
        lastError_.message = "auth.getSession needs a request token";
        return std::nullopt;
    }
    Params params;
    params.emplace_back("method", "auth.getSession");
    params.emplace_back("api_key", credentials_.apiKey);
    params.emplace_back("token", std::string(token));

    const HttpResponse response = post(std::move(params));
    json root;
    if (!decodeResponse(response, root, lastError_)) {
        return std::nullopt;
    }
    const auto sessionIt = root.find("session");
    if (sessionIt == root.end() || !sessionIt->is_object()) {
        lastError_.message = "auth.getSession response carried no session";
        return std::nullopt;
    }
    LastFmSession session;
    session.name = jsonString(*sessionIt, "name");
    session.key = jsonString(*sessionIt, "key");
    if (session.key.empty()) {
        lastError_.message = "auth.getSession returned an empty session key";
        return std::nullopt;
    }
    const auto subscriberIt = sessionIt->find("subscriber");
    if (subscriberIt != sessionIt->end()) {
        if (subscriberIt->is_boolean()) {
            session.subscriber = subscriberIt->get<bool>();
        } else if (subscriberIt->is_number()) {
            session.subscriber = jsonInt(*subscriberIt) != 0;
        }
    }
    return session;
}

bool LastFmClient::updateNowPlaying(const ScrobbleTrack& track) {
    clearError();
    if (!track.valid()) {
        lastError_.message = "updateNowPlaying needs an artist and a title";
        return false;
    }
    if (sessionKey_.empty()) {
        lastError_.message = "no Last.fm session key";
        return false;
    }
    Params params;
    params.emplace_back("method", "track.updateNowPlaying");
    params.emplace_back("api_key", credentials_.apiKey);
    params.emplace_back("sk", sessionKey_);
    appendTrackParams(params, track, -1, 0, false);

    const HttpResponse response = post(std::move(params));
    json root;
    return decodeResponse(response, root, lastError_);
}

bool LastFmClient::scrobble(const std::vector<TimedScrobble>& batch) {
    clearError();
    lastScrobbleResult_ = {};
    if (batch.empty()) {
        lastError_.message = "scrobble batch is empty";
        return false;
    }
    if (batch.size() > 50) {
        lastError_.message = "Last.fm accepts at most 50 scrobbles per batch";
        return false;
    }
    if (sessionKey_.empty()) {
        lastError_.message = "no Last.fm session key";
        return false;
    }
    for (const auto& entry : batch) {
        if (!entry.track.valid()) {
            lastError_.message = "every scrobble needs an artist and a title";
            return false;
        }
    }

    Params params;
    params.emplace_back("method", "track.scrobble");
    params.emplace_back("api_key", credentials_.apiKey);
    params.emplace_back("sk", sessionKey_);
    for (std::size_t i = 0; i < batch.size(); ++i) {
        appendTrackParams(params, batch[i].track, static_cast<int>(i), batch[i].timestampUnix,
                          true);
    }

    const HttpResponse response = post(std::move(params));
    json root;
    if (!decodeResponse(response, root, lastError_)) {
        return false;
    }

    // `{"scrobbles":{"scrobble":[…],"@attr":{"accepted":"2","ignored":"0"}}}`
    LastFmScrobbleResult result;
    result.accepted = static_cast<int>(batch.size());
    const auto scrobblesIt = root.find("scrobbles");
    if (scrobblesIt != root.end() && scrobblesIt->is_object()) {
        const auto attrIt = scrobblesIt->find("@attr");
        if (attrIt != scrobblesIt->end() && attrIt->is_object()) {
            const auto acceptedIt = attrIt->find("accepted");
            if (acceptedIt != attrIt->end()) {
                result.accepted = jsonInt(*acceptedIt);
            }
            const auto ignoredIt = attrIt->find("ignored");
            if (ignoredIt != attrIt->end()) {
                result.ignored = jsonInt(*ignoredIt);
            }
        }
    }
    lastScrobbleResult_ = result;
    return true;
}

} // namespace koalaamp::scrobble
