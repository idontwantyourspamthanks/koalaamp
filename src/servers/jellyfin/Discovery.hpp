// SPDX-License-Identifier: MIT
// jellyfin — LAN discovery via Jellyfin's UDP auto-discovery.
//
// Port of koalapaw `UdpServerDiscovery.kt`: broadcast "who is JellyfinServer?"
// to 255.255.255.255:7359 and collect the JSON replies for a listen window.
// Best-effort — zero, one or several servers may answer. The result-type seam
// here is simply "an empty vector is not an error".

#pragma once

#include "library/ITrackSource.hpp"
#include "servers/jellyfin/JellyfinClient.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace koalaamp::servers::jellyfin {

/// Parse one discovery reply datagram. Returns nullopt when the bytes are not a
/// usable reply (not JSON, or no Address) — the koalapaw `runCatching` + address
/// guard.
std::optional<DiscoveredServer> parseDiscoveryReply(std::string_view bytes);

struct DiscoveryConfig {
    std::uint16_t port{7359};
    std::string broadcastAddress{"255.255.255.255"};
    std::chrono::milliseconds window{2000}; ///< total listen window (koalapaw WINDOW_MS)
    std::chrono::milliseconds slice{300}; ///< receive timeout per wake (koalapaw RECEIVE_SLICE_MS)
};

/// Broadcast the probe and collect replies until the window elapses or the
/// cancellation is requested. Never throws; a socket failure yields no servers.
std::vector<DiscoveredServer>
discoverServers(const DiscoveryConfig& config = {},
                const library::Cancellation& cancel = NoCancellation{});

/// The exact probe payload (koalapaw PROBE).
inline constexpr std::string_view kJellyfinProbe = "who is JellyfinServer?";

} // namespace koalaamp::servers::jellyfin
