// SPDX-License-Identifier: MIT
//
// Util — small, total helpers shared by the playlist format readers/writers:
// bounded file IO, UTF-8 validation/decoding, line splitting and scalar
// parsing. Everything here is exception-free and bounded so the fuzz pass
// (tests/playlist_fuzz_test.cpp) cannot make a parser throw, hang or overrun.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace koalaamp::playlist {

/// Largest playlist/index file we will read (a playlist bigger than this is
/// treated as unreadable rather than trusted). 64 MiB is >10k rich entries.
inline constexpr std::size_t kMaxPlaylistBytes = 64u * 1024u * 1024u;
/// Hard cap on parsed rows, so a pathological file cannot exhaust memory.
inline constexpr std::size_t kMaxTracks = 1000000u;

/// Reads `file` whole, refusing anything above `maxBytes` (returns nullopt on
/// missing/oversized/unreadable input).
std::optional<std::string> readFileBytes(const std::filesystem::path& file,
                                         std::size_t maxBytes = kMaxPlaylistBytes);

/// Writes bytes to `file` atomically: temp file + rename, so a crash (kill -9)
/// never leaves a half-written playlist. Returns false on IO error.
bool writeFileAtomic(const std::filesystem::path& file, std::string_view bytes);

// --- UTF-8 ------------------------------------------------------------------

/// True when `s` is well-formed UTF-8 (rejects overlongs, surrogates, >U+10FFFF).
bool isValidUtf8(std::string_view s);

/// Returns valid UTF-8: passthrough when already valid, otherwise each byte is
/// widened as Latin-1 (U+00NN). Never fails; this is the lenient non-UTF-8 M3U
/// path (Winamp on Windows would use the ANSI codepage; Latin-1 is the
/// platform-independent lossless-enough stand-in).
std::string toValidUtf8(std::string_view bytes);

/// Strips a leading UTF-8 BOM if present; returns the remaining view.
std::string_view stripBom(std::string_view bytes);

// --- text -------------------------------------------------------------------

std::string_view trim(std::string_view s);
std::string toLowerAscii(std::string_view s);
bool equalsIgnoreCase(std::string_view a, std::string_view b);
/// Splits on `\n`, dropping a trailing `\r` on each line (CRLF/LF both work).
std::vector<std::string_view> splitLines(std::string_view text);
/// Splits on `sep` (no empties removed).
std::vector<std::string_view> split(std::string_view s, char sep);
/// Strict integer parse (no trailing garbage). Handles a leading sign.
bool parseLong(std::string_view s, long& out);
/// The final path/URL segment (everything after the last `/` or `\`).
std::string_view filePart(std::string_view path);

// --- XML --------------------------------------------------------------------

/// Escapes `& < > " '` for embedding in text or an attribute value.
std::string xmlEscape(std::string_view s);

/// Prefixes `filename` with `file://` unless it already carries a URL scheme
/// (B4S/XSPF `playstring`/`location`). Mirrors Winamp's file-URL storage.
std::string fileUrlFor(std::string_view filename);
/// Strips a leading `file://` or `file:` prefix (case-insensitive), else identity.
std::string_view stripFileScheme(std::string_view ref);

}  // namespace koalaamp::playlist
