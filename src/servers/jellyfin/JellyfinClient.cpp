// SPDX-License-Identifier: MIT
// jellyfin — gateway result-type seam. See JellyfinClient.hpp.

#include "servers/jellyfin/JellyfinClient.hpp"

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
    if (!response.transportOk()) {
        return AuthUnreachable{};
    }
    if (response.status == 401) {
        return AuthInvalidCredentials{};
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

ItemsOutcome JellyfinClient::musicAlbumsWindow(const std::string& baseUrl, const std::string& token,
                                               const std::string& deviceId,
                                               const std::string& parentId, int startIndex,
                                               int limit, const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "GET";
    std::string query;
    if (!parentId.empty()) {
        query += "parentId=" + encodeQuery(parentId) + "&";
    }
    query += "recursive=true"
             "&includeItemTypes=MusicAlbum"
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

} // namespace koalaamp::servers::jellyfin
