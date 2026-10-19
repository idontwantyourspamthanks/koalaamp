// SPDX-License-Identifier: MIT
// jellyfin — primary artwork fetch with a disk cache.
//
// Jellyfin's image URLs carry the item's `ImageTags.Primary` as `tag=`, which is the
// server's own cache-busting key: a changed image gets a new tag, so the cached file
// for an (item, tag) pair never needs validating. The response's `Cache-Control`
// still decides *when* a fetch is due (`max-age`), and `no-store`/`no-cache` means
// never reuse; a response with no `Cache-Control` is treated as always due, which is
// the safe reading.
//
// One sidecar `<id>-<tag>.meta` per image records the fetch time, the parsed max-age
// and the extension implied by `Content-Type` — the extension is what the caller
// needs to hand a file to QPixmap.

#pragma once

#include "servers/jellyfin/AuthHeaders.hpp"
#include "servers/jellyfin/HttpTransport.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace koalaamp::servers::jellyfin {

class ArtworkCache {
public:
    ArtworkCache(IHttpTransport& transport, DeviceProfile profile, std::filesystem::path dir);

    /// The cached primary image for `itemId` at `imageTag`, fetching it when missing
    /// or due per the recorded `Cache-Control`. Returns the on-disk path, or nullopt
    /// when the server has no image (404), the request fails, or the body is empty.
    std::optional<std::filesystem::path>
    primaryImage(const std::string& baseUrl, const std::string& token,
                 const std::string& deviceId, const std::string& itemId,
                 const std::string& imageTag,
                 const library::Cancellation& cancel = NoCancellation{});

    /// Parse a `Cache-Control` value into seconds. `no-store`/`no-cache`, a malformed
    /// value, or an absent header all yield -1: "never fresh, always refetch".
    static long maxAgeSeconds(const std::string& cacheControl);

private:
    IHttpTransport& transport_;
    DeviceProfile profile_;
    std::filesystem::path dir_;
};

} // namespace koalaamp::servers::jellyfin
