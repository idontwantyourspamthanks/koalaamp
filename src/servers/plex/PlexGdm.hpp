// SPDX-License-Identifier: MIT
// plex — GDM discovery (SV-3). See PlexGdm.cpp for the probe.

#pragma once

#include "library/ITrackSource.hpp"
#include "servers/jellyfin/HttpTransport.hpp"
#include "servers/plex/PlexTypes.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace koalaamp::servers::plex {

/// Parse one GDM reply datagram; `host` is the packet's source address. Null
/// unless it carries both a display name and a Resource-Identifier.
std::optional<PlexDiscoveredServer> parseGdmReply(std::string_view bytes, const std::string& host);

/// Broadcast `M-SEARCH * HTTP/1.1` on UDP 32414 (flat broadcast + GDM multicast
/// group) and collect replies for `window`. Best-effort; empty on failure.
std::vector<PlexDiscoveredServer> discoverPlexServers(
    std::chrono::milliseconds window = std::chrono::seconds(2),
    const library::Cancellation& cancel = jellyfin::NoCancellation{});

} // namespace koalaamp::servers::plex
