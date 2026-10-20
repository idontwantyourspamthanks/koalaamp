// SPDX-License-Identifier: MIT
// plex — PIN/link auth. See PlexAuth.hpp.

#include "servers/plex/PlexAuth.hpp"

#include <nlohmann/json.hpp>
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

bool boolField(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return false;
    }
    if (it->is_boolean()) {
        return it->get<bool>();
    }
    if (it->is_number_integer()) {
        return it->get<int>() != 0;
    }
    if (it->is_string()) {
        const std::string value = it->get<std::string>();
        return value == "1" || value == "true";
    }
    return false;
}

bool providesServer(const json& resource) {
    const auto provides = resource.find("provides");
    if (provides == resource.end()) {
        return true;
    }
    if (provides->is_string()) {
        return provides->get<std::string>().find("server") != std::string::npos;
    }
    if (provides->is_array()) {
        for (const auto& entry : *provides) {
            if (entry.is_string() && entry.get<std::string>().find("server") != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

PlexAuth::PlexAuth(jellyfin::IHttpTransport& transport, PlexClientInfo info, std::string plexTvBase)
    : transport_(transport), info_(std::move(info)),
      plexTvBase_(baseNoSlash(std::move(plexTvBase))) {
    if (info_.deviceName.empty()) {
        info_.deviceName = info_.clientIdentifier;
    }
}

std::string PlexAuth::linkUrl() {
    return "https://plex.tv/link";
}

HttpResponse PlexAuth::get(const std::string& url, const std::string& token,
                           const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "GET";
    request.url = url;
    request.headers = plexHeaders(info_, token);
    request.connectTimeout = std::chrono::seconds(5);
    request.totalTimeout = std::chrono::seconds(8);
    return transport_.perform(request, cancel);
}

HttpResponse PlexAuth::post(const std::string& url,
                            const std::vector<std::pair<std::string, std::string>>& headers,
                            const std::string& body, const library::Cancellation& cancel) {
    HttpRequest request;
    request.method = "POST";
    request.url = url;
    request.headers = headers;
    request.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    request.body = body;
    request.connectTimeout = std::chrono::seconds(5);
    request.totalTimeout = std::chrono::seconds(8);
    return transport_.perform(request, cancel);
}

PlexPinOutcome PlexAuth::createPin(const library::Cancellation& cancel) {
    const HttpResponse response =
        post(plexTvBase_ + "/api/v2/pins", plexHeaders(info_, {}), {}, cancel);
    if (!response.transportOk()) {
        lastError_ = response.error;
        return PlexAuthUnreachable{};
    }
    if (!response.httpOk()) {
        lastError_ = "plex.tv responded with HTTP " + std::to_string(response.status);
        return PlexAuthFailed{lastError_};
    }
    try {
        const json body = json::parse(response.body);
        PlexPin pin{idString(body, "id"), str(body, "code")};
        if (pin.id.empty() || pin.code.empty()) {
            lastError_ = "plex.tv PIN response is missing id/code";
            return PlexAuthFailed{lastError_};
        }
        return pin;
    } catch (const std::exception& error) {
        lastError_ = std::string{"Malformed PIN response: "} + error.what();
        return PlexAuthFailed{lastError_};
    }
}

PlexPollOutcome PlexAuth::pollPin(const std::string& pinId, const library::Cancellation& cancel) {
    const HttpResponse response =
        get(plexTvBase_ + "/api/v2/pins/" + encodeQuery(pinId), {}, cancel);
    if (!response.transportOk()) {
        lastError_ = response.error;
        return PlexAuthUnreachable{};
    }
    if (response.status == 404) {
        return PlexLinkExpired{};
    }
    if (!response.httpOk()) {
        lastError_ = "plex.tv responded with HTTP " + std::to_string(response.status);
        return PlexAuthFailed{lastError_};
    }
    try {
        const json body = json::parse(response.body);
        const std::string token = str(body, "authToken");
        if (token.empty()) {
            return PlexLinkPending{};
        }
        PlexAccount account;
        account.token = token;
        // The profile call is best-effort: the token is what matters.
        const HttpResponse profile = get(plexTvBase_ + "/api/v2/user", token, cancel);
        if (profile.httpOk()) {
            try {
                const json user = json::parse(profile.body);
                account.userId = str(user, "uuid");
                if (account.userId.empty()) {
                    account.userId = idString(user, "id");
                }
                account.userName = str(user, "title");
                if (account.userName.empty()) {
                    account.userName = str(user, "username");
                }
            } catch (const std::exception&) {
                // Keep the token; the account name falls back below.
            }
        }
        if (account.userName.empty()) {
            account.userName = "user";
        }
        return account;
    } catch (const std::exception& error) {
        lastError_ = std::string{"Malformed PIN poll response: "} + error.what();
        return PlexAuthFailed{lastError_};
    }
}

PlexResourcesOutcome PlexAuth::resources(const std::string& accountToken,
                                         const library::Cancellation& cancel) {
    const HttpResponse response =
        get(plexTvBase_ + "/api/v2/resources?includeHttps=1&includeRelay=1", accountToken, cancel);
    if (!response.transportOk()) {
        lastError_ = response.error;
        return PlexAuthUnreachable{};
    }
    if (response.status == 401 || response.status == 403) {
        lastError_ = "plex.tv rejected the account token";
        return PlexAuthFailed{lastError_};
    }
    if (!response.httpOk()) {
        lastError_ = "plex.tv responded with HTTP " + std::to_string(response.status);
        return PlexAuthFailed{lastError_};
    }
    try {
        const json body = json::parse(response.body);
        if (!body.is_array()) {
            lastError_ = "Malformed resources response";
            return PlexAuthFailed{lastError_};
        }
        std::vector<PlexResource> resources;
        for (const auto& dto : body) {
            if (!dto.is_object() || !providesServer(dto)) {
                continue;
            }
            PlexResource resource;
            resource.id = str(dto, "clientIdentifier");
            resource.name = str(dto, "name");
            resource.accessToken = str(dto, "accessToken");
            if (resource.id.empty() || resource.accessToken.empty()) {
                continue; // client-only resources (players) are not attachable
            }
            if (const auto connections = dto.find("connections");
                connections != dto.end() && connections->is_array()) {
                for (const auto& connection : *connections) {
                    if (!connection.is_object()) {
                        continue;
                    }
                    PlexConnection entry;
                    entry.uri = str(connection, "uri");
                    entry.local = boolField(connection, "local");
                    entry.relay = boolField(connection, "relay");
                    if (!entry.uri.empty()) {
                        resource.connections.push_back(std::move(entry));
                    }
                }
            }
            if (!resource.connections.empty()) {
                resources.push_back(std::move(resource));
            }
        }
        return resources;
    } catch (const std::exception& error) {
        lastError_ = std::string{"Malformed resources response: "} + error.what();
        return PlexAuthFailed{lastError_};
    }
}

} // namespace koalaamp::servers::plex
