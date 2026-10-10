// SPDX-License-Identifier: MIT
// jellyfin — MediaBrowser Authorization header construction.
//
// Verbatim port of koalapaw `AuthInterceptor.kt`. The header is required even
// for the login call; `Token` is appended only once a token exists. Auth travels
// in this header only — never a legacy Emby header or a query-string key (both
// deprecated; removed in Jellyfin 12.0 — docs/spec.md §8.2).

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace koalaamp::servers::jellyfin {

/// Stable client identity presented to a Jellyfin server (koalapaw
/// `DeviceProfile.kt`). `deviceId` is not held here: it is per-user and travels
/// with the token so a request never re-binds a shared device.
struct DeviceProfile {
    std::string clientName{"KoalaAmp"};
    std::string clientVersion{"0.0.1"};
    std::string deviceName; ///< e.g. host name; koalapaw uses Build.MODEL
};

/// OS host name, the default for `DeviceProfile::deviceName`.
std::string defaultDeviceName();

/// The exact koalapaw header value:
///   MediaBrowser Client="…", Device="…", DeviceId="…", Version="…"[, Token="…"]
/// `token` empty -> no Token field (login / anonymous calls).
std::string buildAuthHeader(const DeviceProfile& profile, std::string_view deviceId,
                            std::string_view token);

/// All headers every Jellyfin request carries. Content-Type: application/json
/// for bodies; callers add method-specific headers on top.
std::vector<std::pair<std::string, std::string>>
baseHeaders(const DeviceProfile& profile, std::string_view deviceId, std::string_view token);

} // namespace koalaamp::servers::jellyfin
