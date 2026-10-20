// SPDX-License-Identifier: MIT
// plex — PMS browse client. See PlexClient.hpp.

#include "servers/plex/PlexClient.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace koalaamp::servers::plex {
namespace {

using json = nlohmann::json;
using jellyfin::HttpRequest;
using jellyfin::HttpResponse;

std::string baseNoSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

std::string encodeQuery(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
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

std::string str(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

std::string idString(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return {};
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    if (it->is_number_integer()) {
        return std::to_string(it->get<long long>());
    }
    return {};
}

long long num(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return 0;
    }
    if (it->is_number_integer()) {
        return it->get<long long>();
    }
    if (it->is_string()) {
        try {
            return std::stoll(it->get<std::string>());
        } catch (const std::exception&) {
            return 0;
        }
    }
    return 0;
}

std::string keyTail(std::string key) {
    while (!key.empty() && key.back() == '/') {
        key.pop_back();
    }
    const auto slash = key.find_last_of('/');
    return slash == std::string::npos ? key : key.substr(slash + 1);
}

} // namespace

PlexClient::PlexClient(jellyfin::IHttpTransport& transport, PlexClientInfo info,
                       std::string baseUrl, std::string token)
    : transport_(transport),
      info_(std::move(info)),
      baseUrl_(baseNoSlash(std::move(baseUrl))),
      token_(std::move(token)) {
    if (info_.deviceName.empty()) {
        info_.deviceName = info_.clientIdentifier;
    }
}

HttpResponse PlexClient::get(std::string_view path,
                             const std::vector<std::pair<std::string, std::string>>& params,
                             bool includeToken, const library::Cancellation& cancel) {
    std::string url = baseUrl_ + std::string(path);
    if (!params.empty()) {
        url += '?';
        bool first = true;
        for (const auto& [name, value] : params) {
            if (!first) {
                url += '&';
            }
            first = false;
            url += name + "=" + encodeQuery(value);
        }
    }
    HttpRequest request;
    request.method = "GET";
    request.url = std::move(url);
    request.headers = plexHeaders(info_, includeToken ? token_ : std::string_view{});
    for (const auto& header : plexProfileHeaders()) {
        request.headers.push_back(header);
    }
    return transport_.perform(request, cancel);
}

HttpResponse PlexClient::put(std::string_view path,
                             const std::vector<std::pair<std::string, std::string>>& params,
                             const library::Cancellation& cancel) {
    std::string url = baseUrl_ + std::string(path);
    if (!params.empty()) {
        url += '?';
        bool first = true;
        for (const auto& [name, value] : params) {
            if (!first) {
                url += '&';
            }
            first = false;
            url += name + "=" + encodeQuery(value);
        }
    }
    HttpRequest request;
    request.method = "PUT";
    request.url = std::move(url);
    request.headers = plexHeaders(info_, token_);
    for (const auto& header : plexProfileHeaders()) {
        request.headers.push_back(header);
    }
    return transport_.perform(request, cancel);
}

bool PlexClient::ensureOk(const HttpResponse& response, const char* what) {
    if (!response.transportOk()) {
        lastError_ = response.error;
        return false;
    }
    if (response.status == 401) {
        lastError_ = "The Plex server rejected the token";
        return false;
    }
    if (!response.httpOk()) {
        lastError_ = std::string(what) + " failed (HTTP " + std::to_string(response.status) + ")";
        return false;
    }
    lastError_.clear();
    return true;
}

std::optional<PlexServerInfo> PlexClient::identity(const library::Cancellation& cancel) {
    const HttpResponse response = get("/identity", {}, false, cancel);
    if (!ensureOk(response, "identity")) {
        return std::nullopt;
    }
    try {
        const json body = json::parse(response.body);
        const json& container = body.contains("MediaContainer") ? body["MediaContainer"] : body;
        PlexServerInfo info;
        info.id = str(container, "machineIdentifier");
        info.name = str(container, "friendlyName");
        info.version = str(container, "version");
        return info;
    } catch (const std::exception& error) {
        lastError_ = std::string{"Malformed identity response: "} + error.what();
        return std::nullopt;
    }
}

std::optional<std::vector<PlexSection>> PlexClient::sections(
    const library::Cancellation& cancel) {
    const HttpResponse response = get("/library/sections", {}, true, cancel);
    if (!ensureOk(response, "sections")) {
        return std::nullopt;
    }
    try {
        const json body = json::parse(response.body);
        const json& container = body.contains("MediaContainer") ? body["MediaContainer"] : body;
        std::vector<PlexSection> sections;
        if (const auto directories = container.find("Directory");
            directories != container.end() && directories->is_array()) {
            for (const auto& dto : *directories) {
                if (!dto.is_object()) {
                    continue;
                }
                sections.push_back(PlexSection{keyTail(str(dto, "key")), str(dto, "title"),
                                               str(dto, "type")});
            }
        }
        return sections;
    } catch (const std::exception& error) {
        lastError_ = std::string{"Malformed sections response: "} + error.what();
        return std::nullopt;
    }
}

std::optional<std::vector<PlexAlbum>> PlexClient::albums(
    const std::string& sectionId, int start, int limit, const library::Cancellation& cancel) {
    const HttpResponse response =
        get("/library/sections/" + sectionId + "/all",
            {{"type", "9"},
             {"X-Plex-Container-Start", std::to_string(start)},
             {"X-Plex-Container-Size", std::to_string(limit)}},
            true, cancel);
    if (!ensureOk(response, "albums")) {
        return std::nullopt;
    }
    try {
        const json body = json::parse(response.body);
        const json& container = body.contains("MediaContainer") ? body["MediaContainer"] : body;
        std::vector<PlexAlbum> albums;
        if (const auto metadata = container.find("Metadata");
            metadata != container.end() && metadata->is_array()) {
            for (const auto& dto : *metadata) {
                if (!dto.is_object()) {
                    continue;
                }
                albums.push_back(PlexAlbum{idString(dto, "ratingKey"), str(dto, "title"),
                                           str(dto, "parentTitle"), str(dto, "thumb"),
                                           static_cast<int>(num(dto, "year"))});
            }
        }
        return albums;
    } catch (const std::exception& error) {
        lastError_ = std::string{"Malformed albums response: "} + error.what();
        return std::nullopt;
    }
}

library::Track PlexClient::trackFromMetadata(const std::string& dtoJson) {
    library::Track track;
    try {
        const json dto = json::parse(dtoJson);
        track.src = library::SourceId::Plex;
        track.id = idString(dto, "ratingKey");
        track.title = str(dto, "title");
        track.artist = str(dto, "grandparentTitle");
        track.album = str(dto, "parentTitle");
        track.albumArtist = track.artist;
        track.year = static_cast<int>(num(dto, "year"));
        track.trackNo = static_cast<int>(num(dto, "index"));
        track.discNo = static_cast<int>(num(dto, "parentIndex"));
        track.durationMs = num(dto, "duration");
        if (const auto media = dto.find("Media"); media != dto.end() && media->is_array() &&
                                                 !media->empty()) {
            const json& first = media->front();
            track.codec = str(first, "audioCodec");
            if (track.codec.empty()) {
                track.codec = str(first, "container");
            }
            track.bitrate = num(first, "bitrate") * 1000;
            if (const auto parts = first.find("Part");
                parts != first.end() && parts->is_array() && !parts->empty()) {
                track.path = str(parts->front(), "key");
            }
        }
    } catch (const std::exception&) {
        return {};
    }
    return track;
}

std::optional<std::vector<library::Track>> PlexClient::albumTracks(
    const std::string& albumId, const library::Cancellation& cancel) {
    const HttpResponse response = get("/library/metadata/" + albumId + "/children", {}, true,
                                      cancel);
    if (!ensureOk(response, "album tracks")) {
        return std::nullopt;
    }
    try {
        const json body = json::parse(response.body);
        const json& container = body.contains("MediaContainer") ? body["MediaContainer"] : body;
        std::vector<library::Track> tracks;
        if (const auto metadata = container.find("Metadata");
            metadata != container.end() && metadata->is_array()) {
            for (const auto& dto : *metadata) {
                if (!dto.is_object() || str(dto, "type") != "track") {
                    continue;
                }
                tracks.push_back(trackFromMetadata(dto.dump()));
            }
        }
        return tracks;
    } catch (const std::exception& error) {
        lastError_ = std::string{"Malformed track response: "} + error.what();
        return std::nullopt;
    }
}

std::optional<std::vector<library::Track>> PlexClient::search(
    std::string_view query, int limit, const library::Cancellation& cancel) {
    const HttpResponse response =
        get("/hubs/search",
            {{"query", std::string(query)}, {"limit", std::to_string(limit)}}, true, cancel);
    if (!ensureOk(response, "search")) {
        return std::nullopt;
    }
    try {
        const json body = json::parse(response.body);
        const json& container = body.contains("MediaContainer") ? body["MediaContainer"] : body;
        std::vector<library::Track> tracks;
        if (const auto hubs = container.find("Hub");
            hubs != container.end() && hubs->is_array()) {
            for (const auto& hub : *hubs) {
                if (!hub.is_object()) {
                    continue;
                }
                if (const auto metadata = hub.find("Metadata");
                    metadata != hub.end() && metadata->is_array()) {
                    for (const auto& dto : *metadata) {
                        if (!dto.is_object() || str(dto, "type") != "track") {
                            continue;
                        }
                        library::Track track = trackFromMetadata(dto.dump());
                        const bool duplicate =
                            std::any_of(tracks.begin(), tracks.end(),
                                        [&](const library::Track& existing) {
                                            return !existing.id.empty() &&
                                                   existing.id == track.id;
                                        });
                        if (!duplicate) {
                            tracks.push_back(std::move(track));
                        }
                        if (limit > 0 && static_cast<int>(tracks.size()) >= limit) {
                            return tracks;
                        }
                    }
                }
            }
        }
        return tracks;
    } catch (const std::exception& error) {
        lastError_ = std::string{"Malformed search response: "} + error.what();
        return std::nullopt;
    }
}

std::optional<std::string> PlexClient::artwork(const std::string& thumbPath, int width,
                                               int height, const library::Cancellation& cancel) {
    const HttpResponse response =
        (width > 0 || height > 0)
            ? get("/photo/:/transcode",
                  {{"width", width > 0 ? std::to_string(width) : std::string{}},
                   {"height", height > 0 ? std::to_string(height) : std::string{}},
                   {"url", thumbPath}},
                  true, cancel)
            : get(thumbPath, {}, true, cancel);
    if (!ensureOk(response, "artwork")) {
        return std::nullopt;
    }
    return response.body;
}

bool PlexClient::rate(const std::string& ratingKey, int stars,
                      const library::Cancellation& cancel) {
    const int clamped = stars < 0 ? 0 : (stars > 5 ? 5 : stars);
    const int rating = clamped <= 0 ? -1 : clamped * 2;
    const HttpResponse response =
        put("/:/rate",
            {{"key", ratingKey},
             {"identifier", "com.plexapp.plugins.library"},
             {"rating", std::to_string(rating)}},
            cancel);
    return ensureOk(response, "rating");
}

} // namespace koalaamp::servers::plex
