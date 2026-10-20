// SPDX-License-Identifier: MIT
// plex — shared client identity and wire models (SV-3).
//
// The generic HTTP seam is the one in servers/jellyfin/HttpTransport.hpp; it is
// transport-only (curl + fixtures) and is reused here until it moves to a
// neutral servers/net target.

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace koalaamp::servers::plex {

/// Stable per-install identity presented on every plex.tv/PMS request.
struct PlexClientInfo {
    std::string clientIdentifier; ///< X-Plex-Client-Identifier (UUID)
    std::string product{"KoalaAmp"};
    std::string version{"0.0.1"};
    std::string deviceName;
    std::string platform{"Generic"};
};

/// Mandatory X-Plex-* headers; the token is appended only when non-empty.
inline std::vector<std::pair<std::string, std::string>> plexHeaders(const PlexClientInfo& info,
                                                                    std::string_view token) {
    std::vector<std::pair<std::string, std::string>> headers{
        {"X-Plex-Client-Identifier", info.clientIdentifier},
        {"X-Plex-Product", info.product},
        {"X-Plex-Version", info.version},
        {"X-Plex-Device", info.deviceName},
        {"X-Plex-Device-Name", info.deviceName},
        {"X-Plex-Platform", info.platform},
        {"Accept", "application/json"},
    };
    if (!token.empty()) {
        headers.emplace_back("X-Plex-Token", std::string(token));
    }
    return headers;
}

/// Headers that advertise our direct-play codec set to PMS (SV-4). Sent on PMS
/// metadata requests so the server's decisions match what libvlc can decode.
inline std::vector<std::pair<std::string, std::string>> plexProfileHeaders() {
    const std::string extra =
        "add-direct-play-profile(type=musicProfile&container=flac&audioCodec=flac&protocol=http)"
        "+add-direct-play-profile(type=musicProfile&container=mp3&audioCodec=mp3&protocol=http)"
        "+add-direct-play-profile(type=musicProfile&container=aac&audioCodec=aac&protocol=http)"
        "+add-direct-play-profile(type=musicProfile&container=m4a&audioCodec=alac&protocol=http)"
        "+add-direct-play-profile(type=musicProfile&container=ogg&audioCodec=vorbis&protocol=http)"
        "+add-direct-play-profile(type=musicProfile&container=opus&audioCodec=opus&protocol=http)"
        "+add-direct-play-profile(type=musicProfile&container=wav&audioCodec=pcm&protocol=http)";
    return {{"X-Plex-Client-Profile-Name", "KoalaAmp"},
            {"X-Plex-Client-Profile-Extra", extra}};
}

/// True when a PMS `version` is at least `minimum` (dotted numeric compare).
inline bool plexVersionAtLeast(std::string_view version, std::string_view minimum) {
    const auto part = [](std::string_view text, std::size_t index) -> int {
        std::size_t start = 0;
        for (std::size_t i = 0; i < index; ++i) {
            start = text.find('.', start);
            if (start == std::string_view::npos) {
                return 0;
            }
            ++start;
        }
        int value = 0;
        while (start < text.size() && text[start] >= '0' && text[start] <= '9') {
            value = value * 10 + (text[start] - '0');
            ++start;
        }
        return value;
    };
    for (std::size_t i = 0; i < 3; ++i) {
        const int have = part(version, i);
        const int need = part(minimum, i);
        if (have != need) {
            return have > need;
        }
    }
    return true;
}

struct PlexServerInfo {
    std::string id; ///< machineIdentifier
    std::string name;
    std::string version;
};

struct PlexSection {
    std::string id;
    std::string title;
    std::string type; ///< "artist" for music libraries
};

struct PlexAlbum {
    std::string id; ///< ratingKey
    std::string title;
    std::string artist;
    std::string thumb;
    int year{0};
};

struct PlexPin {
    std::string id;
    std::string code; ///< short code the user types at plex.tv/link
};

struct PlexAccount {
    std::string token;
    std::string userId;
    std::string userName;
};

struct PlexConnection {
    std::string uri;
    bool local{false};
    bool relay{false};
};

struct PlexResource {
    std::string id; ///< clientIdentifier, the PMS machineIdentifier
    std::string name;
    std::string accessToken; ///< PMS-scoped token
    std::vector<PlexConnection> connections;

    /// LAN → direct → relay; empty when the resource has no usable connection.
    std::string chooseUri() const {
        const auto pick = [this](bool wantLocal, bool avoidRelay) -> std::string {
            for (const PlexConnection& connection : connections) {
                if (!connection.uri.empty() && connection.local == wantLocal &&
                    (!avoidRelay || !connection.relay)) {
                    return connection.uri;
                }
            }
            return {};
        };
        if (std::string uri = pick(true, true); !uri.empty()) {
            return uri;
        }
        for (const PlexConnection& connection : connections) {
            if (!connection.uri.empty() && !connection.relay) {
                return connection.uri;
            }
        }
        for (const PlexConnection& connection : connections) {
            if (!connection.uri.empty()) {
                return connection.uri;
            }
        }
        return {};
    }
};

struct PlexDiscoveredServer {
    std::string id;
    std::string name;
    std::string address;
};

/// The signed-in PMS session used by the source.
struct PlexSession {
    std::string baseUrl;
    std::string token; ///< PMS-scoped token, never persisted in plain files
    std::string serverId;
    std::string userId;
    std::string userName;
};

} // namespace koalaamp::servers::plex
