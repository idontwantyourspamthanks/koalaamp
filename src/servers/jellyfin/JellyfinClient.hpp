// SPDX-License-Identifier: MIT
// jellyfin — the gateway result-type seam.
//
// Transliteration of koalapaw's `sealed` outcome types (AuthOutcome,
// ItemsOutcome, LibrariesOutcome) to `std::variant`. Nothing throws across this
// boundary: transport errors become Unreachable, HTTP 401 becomes
// InvalidCredentials/Unauthorized, anything else becomes Failed. This is the
// contract spec §8.1 calls "result-type seams".

#pragma once

#include "library/ITrackSource.hpp"
#include "servers/jellyfin/AuthHeaders.hpp"
#include "servers/jellyfin/HttpTransport.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace koalaamp::servers::jellyfin {

/// A Jellyfin item, mapped from BaseItemDto (koalapaw `MediaItem`). The spike
/// carries the album-browse subset; SV-1 grows it.
struct MediaItem {
    std::string id;
    std::string name;
    std::string type;     ///< raw kind, e.g. "MusicAlbum"
    std::string sortName; ///< server sort name (articles stripped)
    std::string parentId;
    std::string albumArtist;
    std::vector<std::string> artists;
    int productionYear{0};
    bool isFolder{false};
    bool played{false};
    bool isFavorite{false};
};

/// One page of an A–Z listing (koalapaw `LibraryWindow`).
struct LibraryWindow {
    int startIndex{0};
    int totalRecordCount{0};
    std::vector<MediaItem> items;
};

/// koalapaw `AuthSession` (the fields the spike needs).
struct AuthSession {
    std::string accessToken;
    std::string userId;
    std::string userName;
    std::string serverId;
};

struct AuthSuccess {
    AuthSession session;
};
struct AuthInvalidCredentials {};
struct AuthUnreachable {};
struct AuthFailed {
    std::string message;
};
using AuthOutcome = std::variant<AuthSuccess, AuthInvalidCredentials, AuthUnreachable, AuthFailed>;

struct ItemsSuccess {
    LibraryWindow window;
};
struct ItemsUnauthorized {};
struct ItemsUnreachable {};
struct ItemsFailed {
    std::string message;
};
using ItemsOutcome = std::variant<ItemsSuccess, ItemsUnauthorized, ItemsUnreachable, ItemsFailed>;

/// One Jellyfin server as seen on the wire (koalapaw `DiscoveredServer`).
struct DiscoveredServer {
    std::string id;
    std::string name;
    std::string address;
};

/// The Jellyfin gateway: authenticates and browses, mapping everything onto the
/// outcome variants above. Holds no per-request state — token/device are passed
/// in, exactly as koalapaw's pinned-client methods do.
class JellyfinClient {
public:
    JellyfinClient(IHttpTransport& transport, DeviceProfile profile);

    /// POST /Users/AuthenticateByName with MediaBrowser headers only (no token).
    /// Mirrors koalapaw `authenticate()` + `mapAuthResponse()`.
    AuthOutcome authenticate(const std::string& baseUrl, const std::string& username,
                             const std::string& password, const std::string& deviceId,
                             const library::Cancellation& cancel = NoCancellation{});

    /// One album page: GET /Items?IncludeItemTypes=MusicAlbum&Recursive=true…
    /// (koalapaw `musicAlbumsWindow` → `atoZWindow`).
    ItemsOutcome musicAlbumsWindow(const std::string& baseUrl, const std::string& token,
                                   const std::string& deviceId, const std::string& parentId,
                                   int startIndex, int limit,
                                   const library::Cancellation& cancel = NoCancellation{});

private:
    IHttpTransport& transport_;
    DeviceProfile profile_;
};

} // namespace koalaamp::servers::jellyfin
