// SPDX-License-Identifier: MIT
// scrobble — MD5 (RFC 1321), used only for Last.fm's api_sig.
//
// Written from RFC 1321. The repo had no MD5 (Sha256 exists in the Jellyfin
// gateway); this one is self-contained so the scrobble module needs no crypto
// dependency and stays Qt-free.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace koalaamp::scrobble {

/// Raw 16-byte digest, little-endian words per RFC 1321.
std::array<std::uint8_t, 16> md5(std::string_view input);

/// Lowercase 32-character hexadecimal digest (the form Last.fm api_sig uses).
std::string md5Hex(std::string_view input);

} // namespace koalaamp::scrobble
