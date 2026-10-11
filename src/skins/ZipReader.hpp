// SPDX-License-Identifier: MIT
//
// Minimal ZIP reader for classic `.wsz` skins (a renamed ZIP, docs/research/
// skins.md §1.1). Supports the two storage methods real skins use: 0 (stored)
// and 8 (deflate, via zlib). Entries are looked up by lower-cased basename,
// which is how Winamp matches skin files across nesting/name-case quirks.
//
// Clean-room: written from the public PKWARE APPNOTE field layout; no Winamp
// source consulted. Promoted from the FE-3 spike into `src/skins/` by SE-1
// (`git mv`, namespace `fe3` -> `koalaamp::skins`); SE-1 added the entry/size
// caps in `readZip` for the fuzz target.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace koalaamp::skins {

/// Raw bytes of a ZIP member plus the metadata the skin loader needs.
struct ZipEntry {
    std::string name;      ///< Original name as stored in the archive.
    std::string basename;  ///< Lower-cased, path-stripped name (e.g. "main.bmp").
    std::vector<std::uint8_t> data;
};

/// Parses the central directory of a ZIP archive held in memory.
/// Returns `std::nullopt` if the archive is malformed (no EOCD / bad offsets).
std::optional<std::vector<ZipEntry>> readZip(const std::vector<std::uint8_t> &bytes);

/// Convenience: reads a file from disk and parses it as a ZIP.
std::optional<std::vector<ZipEntry>> readZipFile(const std::string &path);

}  // namespace koalaamp::skins
