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
#include <map>
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
struct AuthInvalidCredentials {
    /// Jellyfin's own words when it supplies them (its 401 body carries `error`),
    /// otherwise a short generic message. Surfaced by the connection dialog, because
    /// "wrong username or password" is what a *different* rejection also looks like.
    std::string message;
};
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

/// A→Z bucket → the index of its first item in the sorted listing (koalapaw
/// `letterOffsetsFrom`): first occurrence wins, and digits, symbols and blank names
/// share the "#" bucket. The grid uses it to jump without re-paging from zero.
using LetterOffsets = std::map<std::string, int>;

struct OffsetsSuccess {
    LetterOffsets offsets;
};
using OffsetsOutcome =
    std::variant<OffsetsSuccess, ItemsUnauthorized, ItemsUnreachable, ItemsFailed>;

/// Search results mapped onto the player's track model (koalapaw `searchItems`).
struct SearchSuccess {
    std::vector<library::Track> tracks;
};
using SearchOutcome =
    std::variant<SearchSuccess, ItemsUnauthorized, ItemsUnreachable, ItemsFailed>;

/// One server library view (`GET /Users/{userId}/Views`). The browser filters
/// on `collectionType == "music"`; other views (movies, TV) are ignored.
struct MusicLibrary {
    std::string id;
    std::string name;
    std::string collectionType;
};
struct LibrariesSuccess {
    std::vector<MusicLibrary> libraries;
};
using LibrariesOutcome =
    std::variant<LibrariesSuccess, ItemsUnauthorized, ItemsUnreachable, ItemsFailed>;

/// A named non-track item (a playlist, for now — SV-5 keeps playlists read-only).
struct NamedItem {
    std::string id;
    std::string name;
};
struct NamedItemsSuccess {
    std::vector<NamedItem> items;
};
using NamedItemsOutcome =
    std::variant<NamedItemsSuccess, ItemsUnauthorized, ItemsUnreachable, ItemsFailed>;

/// Track listings (album contents, playlist items) mapped like search results.
struct TracksSuccess {
    std::vector<library::Track> tracks;
};
using TracksOutcome =
    std::variant<TracksSuccess, ItemsUnauthorized, ItemsUnreachable, ItemsFailed>;

/// Which playback-report endpoint a report goes to (koalapaw `PlaybackGateway`):
/// start and periodic progress share a payload shape, stop is its own endpoint.
enum class PlaybackEvent { Start, Progress, Stop };

/// One report to the server's session dashboard. `positionTicks` is in 100 ns units
/// (Jellyfin's unit); `playMethod` must be accurate — scrobble plugins read it, and
/// claiming DirectPlay while transcode-serving breaks their accounting.
struct PlaybackReport {
    std::string itemId;
    std::string mediaSourceId; ///< defaults to `itemId` when empty
    std::string playSessionId; ///< one per queue session
    std::string playMethod{"DirectPlay"}; ///< DirectPlay | DirectStream | Transcode
    std::int64_t positionTicks{0};
    bool paused{false};
    bool muted{false};
    int volumeLevel{100}; ///< 0..100, the field server dashboards display
    /// Progress reports name the cause ("TimeUpdate", "Pause", "Unpause", "Seek"…).
    std::string eventName;
};

struct ReportSuccess {};
using ReportOutcome =
    std::variant<ReportSuccess, ItemsUnauthorized, ItemsUnreachable, ItemsFailed>;

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

    /// One artist page: GET /Items?IncludeItemTypes=MusicArtist… — koalapaw
    /// `musicArtistsWindow`, which is `atoZWindow` with the artist kind.
    ItemsOutcome musicArtistsWindow(const std::string& baseUrl, const std::string& token,
                                    const std::string& deviceId, const std::string& parentId,
                                    int startIndex, int limit,
                                    const library::Cancellation& cancel = NoCancellation{});

    /// A→Z offsets for a whole listing: one large page sorted by SortName with images
    /// off, then `letterOffsetsFrom` (koalapaw `musicLetterOffsets` /
    /// `musicArtistLetterOffsets`). `itemType` is the Jellyfin kind, e.g. "MusicAlbum".
    OffsetsOutcome letterOffsets(const std::string& baseUrl, const std::string& token,
                                 const std::string& deviceId, const std::string& parentId,
                                 const std::string& itemType,
                                 const library::Cancellation& cancel = NoCancellation{});

    /// Search the library (koalapaw `searchItems`): `/Items` with a `searchTerm`,
    /// recursive, restricted to `includeItemTypes` (e.g. "Audio"), mapped onto Track.
    SearchOutcome search(const std::string& baseUrl, const std::string& token,
                         const std::string& deviceId, const std::string& parentId,
                         const std::string& query, const std::string& includeItemTypes,
                         int limit, const library::Cancellation& cancel = NoCancellation{});

    /// Music libraries visible to the user (`GET /Users/{userId}/Views`).
    LibrariesOutcome musicLibraries(const std::string& baseUrl, const std::string& token,
                                    const std::string& deviceId, const std::string& userId,
                                    const library::Cancellation& cancel = NoCancellation{});

    /// Album contents in disc/track order (`ParentId` + `Audio`, recursive).
    TracksOutcome albumTracks(const std::string& baseUrl, const std::string& token,
                              const std::string& deviceId, const std::string& albumId,
                              const library::Cancellation& cancel = NoCancellation{});

    /// Playlists visible to the user (`IncludeItemTypes=Playlist`). Read-only.
    NamedItemsOutcome playlists(const std::string& baseUrl, const std::string& token,
                                const std::string& deviceId,
                                const library::Cancellation& cancel = NoCancellation{});

    /// Playlist contents in playlist order (`GET /Playlists/{id}/Items`).
    TracksOutcome playlistItems(const std::string& baseUrl, const std::string& token,
                                const std::string& deviceId, const std::string& playlistId,
                                const std::string& userId,
                                const library::Cancellation& cancel = NoCancellation{});

    /// Report playback to the server (koalapaw `PlaybackGateway`): POST to
    /// `/Sessions/Playing`, `/Sessions/Playing/Progress` or `/Sessions/Playing/Stopped`
    /// per `event`. The token travels in the MediaBrowser header only.
    ReportOutcome reportPlayback(const std::string& baseUrl, const std::string& token,
                                 const std::string& deviceId, PlaybackEvent event,
                                 const PlaybackReport& report,
                                 const library::Cancellation& cancel = NoCancellation{});

    /// Set a user rating (`stars` 0..5, sent as Jellyfin's 0..10 scale) through
    /// `POST /Users/{userId}/Items/{itemId}/UserData`.
    ReportOutcome setRating(const std::string& baseUrl, const std::string& token,
                            const std::string& deviceId, const std::string& userId,
                            const std::string& itemId, int stars,
                            const library::Cancellation& cancel = NoCancellation{});

private:
    /// The shared A→Z page read behind `musicAlbumsWindow`/`musicArtistsWindow`
    /// (koalapaw `atoZWindow`): recursive, SortName ascending, one item type.
    ItemsOutcome atoZWindow(const std::string& baseUrl, const std::string& token,
                            const std::string& deviceId, const std::string& parentId,
                            const std::string& itemType, int startIndex, int limit,
                            const library::Cancellation& cancel);

    IHttpTransport& transport_;
    DeviceProfile profile_;
};

} // namespace koalaamp::servers::jellyfin
