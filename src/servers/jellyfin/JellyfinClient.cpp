// SPDX-License-Identifier: MIT
// jellyfin — gateway result-type seam. See JellyfinClient.hpp.

#include "servers/jellyfin/JellyfinClient.hpp"

#include <cctype>
#include <nlohmann/json.hpp>
#include <utility>

namespace koalaamp::servers::jellyfin {
namespace {

using json = nlohmann::json;

/// Percent-encode a query value (RFC 3986 unreserved set left alone).
std::string encodeQuery(std::string_view value) {
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

std::string baseUrlNoSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

std::string str(const json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return {};
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    return {};
}

MediaItem toMediaItem(const json& dto) {
    MediaItem item;
    item.id = str(dto, "Id");
    item.name = str(dto, "Name");
    item.type = str(dto, "Type");
    item.sortName = str(dto, "SortName");
    item.parentId = str(dto, "ParentId");
    item.albumArtist = str(dto, "AlbumArtist");
    if (const auto it = dto.find("Artists"); it != dto.end() && it->is_array()) {
        for (const auto& a : *it) {
            if (a.is_string()) {
                item.artists.push_back(a.get<std::string>());
            }
        }
    }
    if (const auto it = dto.find("ProductionYear"); it != dto.end() && it->is_number_integer()) {
        item.productionYear = it->get<int>();
    }
    item.isFolder = dto.value("IsFolder", false);
    if (const auto ud = dto.find("UserData"); ud != dto.end() && ud->is_object()) {
        item.played = ud->value("Played", false);
        item.isFavorite = ud->value("IsFavorite", false);
    }
    return item;
}

/// One `/Items` DTO -> the player's track model. Search, album contents and
/// playlist items all share this mapping so a track looks the same wherever it
/// came from.
library::Track trackFromDto(const json& dto) {
    library::Track track;
    track.src = library::SourceId::Jellyfin;
    track.id = str(dto, "Id");
    track.title = str(dto, "Name");
    track.album = str(dto, "Album");
    track.albumArtist = str(dto, "AlbumArtist");
    if (const auto artists = dto.find("Artists"); artists != dto.end() && artists->is_array() &&
                                                  !artists->empty() &&
                                                  artists->front().is_string()) {
        track.artist = artists->front().get<std::string>();
    }
    if (track.artist.empty()) {
        track.artist = track.albumArtist;
    }
    track.year = dto.value("ProductionYear", 0);
    track.trackNo = dto.value("IndexNumber", 0);
    track.discNo = dto.value("ParentIndexNumber", 0);
    // Jellyfin reports RunTimeTicks in 100 ns units.
    track.durationMs = static_cast<std::int64_t>(dto.value("RunTimeTicks", 0LL) / 10000LL);
    track.codec = str(dto, "Container");
    if (const auto bitrate = dto.find("Bitrate");
        bitrate != dto.end() && bitrate->is_number_integer()) {
        track.bitrate = bitrate->get<std::int64_t>();
    }
    // SC-1: Jellyfin exposes MusicBrainz as ProviderIds; carry them so
    // ListenBrainz can submit with recording/release/artist MBIDs.
    if (const auto ids = dto.find("ProviderIds"); ids != dto.end() && ids->is_object()) {
        track.recordingMbid = str(*ids, "MusicBrainzTrack");
        track.releaseMbid = str(*ids, "MusicBrainzAlbum");
        const std::string artists = str(*ids, "MusicBrainzArtist");
        std::string currentId;
        for (std::size_t i = 0; i <= artists.size(); ++i) {
            if (i == artists.size() || artists[i] == ';' || artists[i] == ',') {
                if (!currentId.empty()) {
                    track.artistMbids.push_back(currentId);
                    currentId.clear();
                }
            } else if (artists[i] != ' ') {
                currentId.push_back(artists[i]);
            }
        }
    }

    // One normalization gain per item (computed from the file's ReplayGain tag
    // server-side); album mode falls back to it.
    if (const auto gain = dto.find("NormalizationGain"); gain != dto.end() && gain->is_number()) {
        library::Rg rg;
        rg.trackGain = gain->get<double>();
        track.rg = rg;
    }
    return track;
}

/// Shared mapping for the two auth endpoints (password + QuickConnect).
AuthOutcome parseAuthResponse(const HttpResponse& response) {
    if (!response.transportOk()) {
        return AuthUnreachable{};
    }
    if (response.status == 401) {
        // Jellyfin's 401 body carries the reason; we used to drop it, which made a
        // rejected *header* indistinguishable from a wrong password.
        std::string message = "The server rejected the credentials";
        if (!response.body.empty()) {
            try {
                const json body = json::parse(response.body);
                const auto it = body.find("error");
                if (it != body.end() && it->is_string()) {
                    message = it->get<std::string>();
                } else {
                    message = response.body.substr(0, 200);
                }
            } catch (const std::exception&) {
                message = response.body.substr(0, 200);
            }
        }
        return AuthInvalidCredentials{std::move(message)};
    }
    if (!response.httpOk()) {
        return AuthFailed{"Login failed (HTTP " + std::to_string(response.status) + ")"};
    }

    try {
        const json body = json::parse(response.body);
        const std::string token = str(body, "AccessToken");
        const auto user = body.find("User");
        if (token.empty() || user == body.end() || !user->is_object()) {
            return AuthFailed{"Login failed (incomplete response)"};
        }
        AuthSession session;
        session.accessToken = token;
        session.userId = str(*user, "Id");
        session.userName = str(*user, "Name");
        if (session.userName.empty()) {
            session.userName = "user";
        }
        session.serverId = str(body, "ServerId");
        return AuthSuccess{std::move(session)};
    } catch (const std::exception& e) {
        return AuthFailed{std::string{"Malformed login response: "} + e.what()};
    }
}

} // namespace

JellyfinClient::JellyfinClient(IHttpTransport& transport, DeviceProfile profile)
    : transport_(transport), profile_(std::move(profile)) {
    if (profile_.deviceName.empty()) {
        profile_.deviceName = defaultDeviceName();
    }
}

AuthOutcome JellyfinClient::authenticate(const std::string& baseUrl, const std::string& username,
                                         const std::string& password, const std::string& deviceId,
                                         const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "POST";
    request.url = baseUrlNoSlash(baseUrl) + "/Users/AuthenticateByName";
    request.headers = baseHeaders(profile_, deviceId, /*token=*/{});
    request.headers.emplace_back("Content-Type", "application/json");
    request.body = json{{"Username", username}, {"Pw", password}}.dump();

    const HttpResponse response = transport_.perform(request, cancel);
    return parseAuthResponse(response);
}

bool JellyfinClient::quickConnectEnabled(const std::string& baseUrl, const std::string& deviceId,
                                         const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "GET";
    request.url = baseUrlNoSlash(baseUrl) + "/QuickConnect/Enabled";
    request.headers = baseHeaders(profile_, deviceId, /*token=*/{});
    const HttpResponse response = transport_.perform(request, cancel);
    return response.httpOk() && response.body.find("true") != std::string::npos;
}

QuickConnectStartOutcome JellyfinClient::initiateQuickConnect(const std::string& baseUrl,
                                                              const std::string& deviceId,
                                                              const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "POST";
    request.url = baseUrlNoSlash(baseUrl) + "/QuickConnect/Initiate";
    request.headers = baseHeaders(profile_, deviceId, /*token=*/{});
    request.connectTimeout = std::chrono::seconds(5);
    request.totalTimeout = std::chrono::seconds(8);

    const HttpResponse response = transport_.perform(request, cancel);
    if (!response.transportOk()) {
        return ItemsUnreachable{};
    }
    if (response.status == 401) {
        return ItemsUnauthorized{};
    }
    if (!response.httpOk()) {
        return ItemsFailed{"QuickConnect is not available (HTTP " +
                           std::to_string(response.status) + ")"};
    }
    try {
        const json body = json::parse(response.body);
        QuickConnectStart start{str(body, "Secret"), str(body, "Code")};
        if (start.secret.empty() || start.code.empty()) {
            return ItemsFailed{"QuickConnect response missing secret/code"};
        }
        return start;
    } catch (const std::exception& e) {
        return ItemsFailed{std::string{"Malformed QuickConnect response: "} + e.what()};
    }
}

QuickConnectStateOutcome JellyfinClient::quickConnectState(const std::string& baseUrl,
                                                           const std::string& deviceId,
                                                           const std::string& secret,
                                                           const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "GET";
    request.url = baseUrlNoSlash(baseUrl) + "/QuickConnect/Connect?secret=" + encodeQuery(secret);
    request.headers = baseHeaders(profile_, deviceId, /*token=*/{});
    request.connectTimeout = std::chrono::seconds(5);
    request.totalTimeout = std::chrono::seconds(8);

    const HttpResponse response = transport_.perform(request, cancel);
    if (!response.transportOk()) {
        return ItemsUnreachable{};
    }
    if (response.status == 401) {
        return ItemsUnauthorized{};
    }
    if (!response.httpOk()) {
        return ItemsFailed{"QuickConnect poll failed (HTTP " + std::to_string(response.status) +
                           ")"};
    }
    try {
        const json body = json::parse(response.body);
        QuickConnectState state;
        state.authenticated = body.value("Authenticated", false);
        state.secret = str(body, "Secret");
        state.code = str(body, "Code");
        return state;
    } catch (const std::exception& e) {
        return ItemsFailed{std::string{"Malformed QuickConnect state: "} + e.what()};
    }
}

AuthOutcome JellyfinClient::authenticateWithQuickConnect(const std::string& baseUrl,
                                                         const std::string& secret,
                                                         const std::string& deviceId,
                                                         const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "POST";
    request.url = baseUrlNoSlash(baseUrl) + "/Users/AuthenticateWithQuickConnect";
    request.headers = baseHeaders(profile_, deviceId, /*token=*/{});
    request.headers.emplace_back("Content-Type", "application/json");
    request.connectTimeout = std::chrono::seconds(5);
    request.totalTimeout = std::chrono::seconds(8);
    request.body = json{{"Secret", secret}}.dump();

    const HttpResponse response = transport_.perform(request, cancel);
    return parseAuthResponse(response);
}

ItemsOutcome JellyfinClient::atoZWindow(const std::string& baseUrl, const std::string& token,
                                        const std::string& deviceId, const std::string& parentId,
                                        const std::string& itemType, int startIndex, int limit,
                                        const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "GET";
    std::string query;
    if (!parentId.empty()) {
        query += "parentId=" + encodeQuery(parentId) + "&";
    }
    query += "recursive=true&includeItemTypes=" + encodeQuery(itemType) +
             "&sortBy=SortName"
             "&sortOrder=Ascending"
             "&startIndex=" +
             std::to_string(startIndex) + "&limit=" + std::to_string(limit) +
             "&fields=SortName"
             "&enableUserData=true";
    request.url = baseUrlNoSlash(baseUrl) + "/Items?" + query;
    request.headers = baseHeaders(profile_, deviceId, token);

    const HttpResponse response = transport_.perform(request, cancel);
    if (!response.transportOk()) {
        return ItemsUnreachable{};
    }
    if (response.status == 401) {
        return ItemsUnauthorized{};
    }
    if (!response.httpOk()) {
        return ItemsFailed{"Server responded with HTTP " + std::to_string(response.status)};
    }

    try {
        const json body = json::parse(response.body);
        LibraryWindow window;
        window.startIndex = body.value("StartIndex", startIndex);
        window.totalRecordCount = body.value("TotalRecordCount", 0);
        if (const auto it = body.find("Items"); it != body.end() && it->is_array()) {
            for (const auto& dto : *it) {
                if (dto.is_object() && !str(dto, "Id").empty()) {
                    window.items.push_back(toMediaItem(dto));
                }
            }
        }
        return ItemsSuccess{std::move(window)};
    } catch (const std::exception& e) {
        return ItemsFailed{std::string{"Malformed items response: "} + e.what()};
    }
}

ItemsOutcome JellyfinClient::musicAlbumsWindow(const std::string& baseUrl, const std::string& token,
                                               const std::string& deviceId,
                                               const std::string& parentId, int startIndex,
                                               int limit, const library::Cancellation& cancel) {
    return atoZWindow(baseUrl, token, deviceId, parentId, "MusicAlbum", startIndex, limit, cancel);
}

ItemsOutcome JellyfinClient::musicArtistsWindow(const std::string& baseUrl,
                                                const std::string& token,
                                                const std::string& deviceId,
                                                const std::string& parentId, int startIndex,
                                                int limit, const library::Cancellation& cancel) {
    return atoZWindow(baseUrl, token, deviceId, parentId, "MusicArtist", startIndex, limit, cancel);
}

namespace {

/// koalapaw `letterBucket`: the first character, upper-cased; digits, symbols and
/// blank names share the "#" bucket.
std::string letterBucket(const std::string& sortName, const std::string& name) {
    const std::string& source = sortName.empty() ? name : sortName;
    const auto first = source.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "#";
    }
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(source[first])));
    return (c >= 'A' && c <= 'Z') ? std::string(1, c) : std::string{"#"};
}

} // namespace

OffsetsOutcome JellyfinClient::letterOffsets(const std::string& baseUrl, const std::string& token,
                                             const std::string& deviceId,
                                             const std::string& parentId,
                                             const std::string& itemType,
                                             const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "GET";
    std::string query;
    if (!parentId.empty()) {
        query += "parentId=" + encodeQuery(parentId) + "&";
    }
    // koalapaw `atoZLetterOffsets`: one page big enough for the whole listing, no images.
    query += "recursive=true&includeItemTypes=" + encodeQuery(itemType) +
             "&sortBy=SortName"
             "&sortOrder=Ascending"
             "&startIndex=0"
             "&limit=100000"
             "&fields=SortName"
             "&enableImages=false";
    request.url = baseUrlNoSlash(baseUrl) + "/Items?" + query;
    request.headers = baseHeaders(profile_, deviceId, token);

    const HttpResponse response = transport_.perform(request, cancel);
    if (!response.transportOk()) {
        return ItemsUnreachable{};
    }
    if (response.status == 401) {
        return ItemsUnauthorized{};
    }
    if (!response.httpOk()) {
        return ItemsFailed{"Server responded with HTTP " + std::to_string(response.status)};
    }

    try {
        const json body = json::parse(response.body);
        OffsetsSuccess success;
        const auto it = body.find("Items");
        if (it != body.end() && it->is_array()) {
            int index = 0;
            for (const auto& dto : *it) {
                if (!dto.is_object()) {
                    continue;
                }
                // koalapaw's `putIfAbsent`: the first item of a bucket wins.
                success.offsets.emplace(letterBucket(str(dto, "SortName"), str(dto, "Name")),
                                        index);
                ++index;
            }
        }
        return success;
    } catch (const std::exception& e) {
        return ItemsFailed{std::string{"Malformed items response: "} + e.what()};
    }
}

SearchOutcome JellyfinClient::search(const std::string& baseUrl, const std::string& token,
                                     const std::string& deviceId, const std::string& parentId,
                                     const std::string& query, const std::string& includeItemTypes,
                                     int limit, const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "GET";
    std::string params;
    if (!parentId.empty()) {
        params += "parentId=" + encodeQuery(parentId) + "&";
    }
    params += "searchTerm=" + encodeQuery(query) +
              "&recursive=true&includeItemTypes=" + encodeQuery(includeItemTypes) +
              "&sortBy=SortName"
              "&sortOrder=Ascending"
              "&limit=" +
              std::to_string(limit) +
              "&fields=SortName,RunTimeTicks,Album,AlbumArtist,Artists,ProductionYear,IndexNumber,"
              "ParentIndexNumber,Container,NormalizationGain,ProviderIds";
    request.url = baseUrlNoSlash(baseUrl) + "/Items?" + params;
    request.headers = baseHeaders(profile_, deviceId, token);

    const HttpResponse response = transport_.perform(request, cancel);
    if (!response.transportOk()) {
        return ItemsUnreachable{};
    }
    if (response.status == 401) {
        return ItemsUnauthorized{};
    }
    if (!response.httpOk()) {
        return ItemsFailed{"Server responded with HTTP " + std::to_string(response.status)};
    }

    try {
        const json body = json::parse(response.body);
        SearchSuccess success;
        const auto it = body.find("Items");
        if (it != body.end() && it->is_array()) {
            for (const auto& dto : *it) {
                if (!dto.is_object() || str(dto, "Id").empty()) {
                    continue;
                }
                success.tracks.push_back(trackFromDto(dto));
            }
        }
        return success;
    } catch (const std::exception& e) {
        return ItemsFailed{std::string{"Malformed items response: "} + e.what()};
    }
}

ReportOutcome JellyfinClient::reportPlayback(const std::string& baseUrl, const std::string& token,
                                             const std::string& deviceId, PlaybackEvent event,
                                             const PlaybackReport& report,
                                             const library::Cancellation& cancel) {
    const char* path = "/Sessions/Playing";
    if (event == PlaybackEvent::Progress) {
        path = "/Sessions/Playing/Progress";
    } else if (event == PlaybackEvent::Stop) {
        path = "/Sessions/Playing/Stopped";
    }

    json body{{"ItemId", report.itemId},           {"PlaySessionId", report.playSessionId},
              {"PlayMethod", report.playMethod},   {"PositionTicks", report.positionTicks},
              {"IsPaused", report.paused},         {"IsMuted", report.muted},
              {"VolumeLevel", report.volumeLevel}, {"CanSeek", true}};
    body["MediaSourceId"] = report.mediaSourceId.empty() ? report.itemId : report.mediaSourceId;
    if (!report.eventName.empty()) {
        body["EventName"] = report.eventName;
    }

    HttpRequest request;
    request.method = "POST";
    request.url = baseUrlNoSlash(baseUrl) + path;
    request.headers = baseHeaders(profile_, deviceId, token);
    request.headers.emplace_back("Content-Type", "application/json");
    request.body = body.dump();

    const HttpResponse response = transport_.perform(request, cancel);
    if (!response.transportOk()) {
        return ItemsUnreachable{};
    }
    if (response.status == 401) {
        return ItemsUnauthorized{};
    }
    if (!response.httpOk()) {
        return ItemsFailed{"Server responded with HTTP " + std::to_string(response.status)};
    }
    return ReportSuccess{};
}

LibrariesOutcome JellyfinClient::musicLibraries(const std::string& baseUrl,
                                                const std::string& token,
                                                const std::string& deviceId,
                                                const std::string& userId,
                                                const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "GET";
    request.url = baseUrlNoSlash(baseUrl) + "/Users/" + encodeQuery(userId) + "/Views";
    request.headers = baseHeaders(profile_, deviceId, token);

    const HttpResponse response = transport_.perform(request, cancel);
    if (!response.transportOk()) {
        return ItemsUnreachable{};
    }
    if (response.status == 401) {
        return ItemsUnauthorized{};
    }
    if (!response.httpOk()) {
        return ItemsFailed{"Server responded with HTTP " + std::to_string(response.status)};
    }

    try {
        const json body = json::parse(response.body);
        LibrariesSuccess success;
        const auto it = body.find("Items");
        if (it != body.end() && it->is_array()) {
            for (const auto& dto : *it) {
                if (!dto.is_object()) {
                    continue;
                }
                success.libraries.push_back(
                    MusicLibrary{str(dto, "Id"), str(dto, "Name"), str(dto, "CollectionType")});
            }
        }
        return success;
    } catch (const std::exception& e) {
        return ItemsFailed{std::string{"Malformed views response: "} + e.what()};
    }
}

TracksOutcome JellyfinClient::albumTracks(const std::string& baseUrl, const std::string& token,
                                          const std::string& deviceId, const std::string& albumId,
                                          const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "GET";
    const std::string params =
        "parentId=" + encodeQuery(albumId) +
        "&recursive=true&includeItemTypes=Audio"
        "&sortBy=ParentIndexNumber,IndexNumber&sortOrder=Ascending"
        "&fields=SortName,RunTimeTicks,Album,AlbumArtist,Artists,ProductionYear,IndexNumber,"
        "ParentIndexNumber,Container,NormalizationGain,ProviderIds";
    request.url = baseUrlNoSlash(baseUrl) + "/Items?" + params;
    request.headers = baseHeaders(profile_, deviceId, token);

    const HttpResponse response = transport_.perform(request, cancel);
    if (!response.transportOk()) {
        return ItemsUnreachable{};
    }
    if (response.status == 401) {
        return ItemsUnauthorized{};
    }
    if (!response.httpOk()) {
        return ItemsFailed{"Server responded with HTTP " + std::to_string(response.status)};
    }

    try {
        const json body = json::parse(response.body);
        TracksSuccess success;
        const auto it = body.find("Items");
        if (it != body.end() && it->is_array()) {
            for (const auto& dto : *it) {
                if (!dto.is_object() || str(dto, "Id").empty()) {
                    continue;
                }
                success.tracks.push_back(trackFromDto(dto));
            }
        }
        return success;
    } catch (const std::exception& e) {
        return ItemsFailed{std::string{"Malformed album response: "} + e.what()};
    }
}

NamedItemsOutcome JellyfinClient::playlists(const std::string& baseUrl, const std::string& token,
                                            const std::string& deviceId,
                                            const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "GET";
    request.url = baseUrlNoSlash(baseUrl) +
                  "/Items?includeItemTypes=Playlist&recursive=true&sortBy=SortName"
                  "&sortOrder=Ascending&fields=SortName";
    request.headers = baseHeaders(profile_, deviceId, token);

    const HttpResponse response = transport_.perform(request, cancel);
    if (!response.transportOk()) {
        return ItemsUnreachable{};
    }
    if (response.status == 401) {
        return ItemsUnauthorized{};
    }
    if (!response.httpOk()) {
        return ItemsFailed{"Server responded with HTTP " + std::to_string(response.status)};
    }

    try {
        const json body = json::parse(response.body);
        NamedItemsSuccess success;
        const auto it = body.find("Items");
        if (it != body.end() && it->is_array()) {
            for (const auto& dto : *it) {
                if (!dto.is_object()) {
                    continue;
                }
                success.items.push_back(NamedItem{str(dto, "Id"), str(dto, "Name")});
            }
        }
        return success;
    } catch (const std::exception& e) {
        return ItemsFailed{std::string{"Malformed playlists response: "} + e.what()};
    }
}

TracksOutcome JellyfinClient::playlistItems(const std::string& baseUrl, const std::string& token,
                                            const std::string& deviceId,
                                            const std::string& playlistId,
                                            const std::string& userId,
                                            const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "GET";
    request.url = baseUrlNoSlash(baseUrl) + "/Playlists/" + encodeQuery(playlistId) +
                  "/Items?userId=" + encodeQuery(userId) +
                  "&fields=SortName,RunTimeTicks,Album,AlbumArtist,Artists,ProductionYear,"
                  "IndexNumber,ParentIndexNumber,Container,NormalizationGain,ProviderIds";
    request.headers = baseHeaders(profile_, deviceId, token);

    const HttpResponse response = transport_.perform(request, cancel);
    if (!response.transportOk()) {
        return ItemsUnreachable{};
    }
    if (response.status == 401) {
        return ItemsUnauthorized{};
    }
    if (!response.httpOk()) {
        return ItemsFailed{"Server responded with HTTP " + std::to_string(response.status)};
    }

    try {
        const json body = json::parse(response.body);
        TracksSuccess success;
        const auto it = body.find("Items");
        if (it != body.end() && it->is_array()) {
            for (const auto& dto : *it) {
                if (!dto.is_object() || str(dto, "Id").empty()) {
                    continue;
                }
                success.tracks.push_back(trackFromDto(dto));
            }
        }
        return success;
    } catch (const std::exception& e) {
        return ItemsFailed{std::string{"Malformed playlist response: "} + e.what()};
    }
}

ReportOutcome JellyfinClient::setRating(const std::string& baseUrl, const std::string& token,
                                        const std::string& deviceId, const std::string& userId,
                                        const std::string& itemId, int stars,
                                        const library::Cancellation& cancel) {
    const int clamped = stars < 0 ? 0 : (stars > 5 ? 5 : stars);
    HttpRequest request;
    request.method = "POST";
    request.url = baseUrlNoSlash(baseUrl) + "/Users/" + encodeQuery(userId) + "/Items/" +
                  encodeQuery(itemId) + "/UserData";
    request.headers = baseHeaders(profile_, deviceId, token);
    request.headers.emplace_back("Content-Type", "application/json");
    request.body = json{{"Rating", static_cast<double>(clamped) * 2.0}}.dump();

    const HttpResponse response = transport_.perform(request, cancel);
    if (!response.transportOk()) {
        return ItemsUnreachable{};
    }
    if (response.status == 401) {
        return ItemsUnauthorized{};
    }
    if (!response.httpOk()) {
        return ItemsFailed{"Server responded with HTTP " + std::to_string(response.status)};
    }
    return ReportSuccess{};
}

} // namespace koalaamp::servers::jellyfin
