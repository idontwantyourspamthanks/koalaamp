// SPDX-License-Identifier: MIT
// jellyfin — SHA-256 (FIPS 180-4) for the per-user DeviceId derivation.
// Self-contained: the spike needs one hash and must not pull a crypto
// dependency into the app for it. Public reference algorithm, written from
// the FIPS 180-4 specification.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace koalaamp::servers::jellyfin {

/// Lowercase hex SHA-256 of `input` (64 chars).
std::string sha256Hex(std::string_view input);

} // namespace koalaamp::servers::jellyfin
