// SPDX-License-Identifier: MIT
//
// Playlist — the serialisation layer for every playlist format a Winamp user's
// disk contains (docs/tickets/PL-1-playlist-formats.md, docs/research/
// formats.md §1-2). Qt-FREE by contract (like src/audio): it depends on
// nothing but the standard library.
//
//   read:  M3U/M3U8 (BOM-aware, lenient), PLS, B4S, XSPF, ASX/WPL
//   write: M3U/M3U8 (byte-exact `#EXTINF` emission + BOM), PLS, B4S, XSPF
//   index: Winamp Media Library `playlists.xml` read + write
//   misc:  autosave state (plain M3U8 + queue/index sidecar), extension list
//
// The writers are the inverse of the readers: the per-format suites assert
// write -> read -> model equality, and the M3U8 writer is asserted byte-for-
// byte against a source-verified fixture (including the `EF BB BF` BOM that
// must precede `#EXTM3U`, and UTF-8 titles).
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "playlist/Track.hpp"

namespace koalaamp::playlist {

// --- path handling ----------------------------------------------------------

/// Stable, canonical key for a path (the `SkinDb::keyFor` pattern): canonicalise
/// once, store the canonical form, never compare a raw path to a canonical one.
std::string canonicalKey(const std::filesystem::path& path);

/// Resolves a media reference from a playlist: URLs, UNC paths and Windows
/// drive paths are returned unchanged; a relative path is joined to `baseDir`
/// and canonicalised. Empty input yields empty output.
std::string resolveMediaRef(std::string_view ref, const std::filesystem::path& baseDir);

/// The display title used when a format carries no title: the file part of
/// `filename` without its extension (Winamp's fallback).
std::string titleFromFilename(std::string_view filename);

// --- M3U / M3U8 -------------------------------------------------------------

/// Exact Winamp emission. `utf8` prepends the `EF BB BF` BOM (M3U8) and encodes
/// nothing extra; the header is always `#EXTM3U\n`. A track with no metadata
/// (`title.empty() && lengthMs == 0`) is written as a bare filename line.
std::string writeM3u(const std::vector<Track>& tracks, bool utf8);

std::vector<Track> readM3u(std::string_view bytes, const ReadOptions& opts);

// --- PLS --------------------------------------------------------------------

std::string writePls(const std::vector<Track>& tracks);
std::vector<Track> readPls(std::string_view bytes, const ReadOptions& opts);

// --- B4S --------------------------------------------------------------------

std::string writeB4s(const std::vector<Track>& tracks);
std::vector<Track> readB4s(std::string_view bytes, const ReadOptions& opts);

// --- XSPF -------------------------------------------------------------------

std::string writeXspf(const std::vector<Track>& tracks);
std::vector<Track> readXspf(std::string_view bytes, const ReadOptions& opts);

// --- ASX / WPL (read-only; recorded choice — see Asx.cpp) -------------------

std::vector<Track> readAsx(std::string_view bytes, const ReadOptions& opts);
std::vector<Track> readWpl(std::string_view bytes, const ReadOptions& opts);

// --- Media Library `playlists.xml` index ------------------------------------

struct IndexEntry {
    std::string filename;  ///< the referenced .m3u, as stored in the index
    std::string title;
    long songs = 0;
    long seconds = 0;
    std::string id;        ///< `{GUID}` as stored
    bool operator==(const IndexEntry&) const = default;
};

std::vector<IndexEntry> readIndex(std::string_view bytes);
std::string writeIndex(const std::vector<IndexEntry>& entries);
/// A fresh `{GUID}` (v4-shaped) for new index entries.
std::string newGuid();

// --- format dispatch --------------------------------------------------------

/// Maps a file extension (with or without the dot, any case) to a format.
Format formatForExtension(std::string_view ext);
/// The extensions we can *read* (the file-association list, CO-1 consumes this).
const std::vector<std::string>& supportedExtensions();
/// The canonical extension for a format (".m3u8" for M3u8, …), "" if unknown.
std::string extensionForFormat(Format format);

/// Reads `file`, choosing the format from its extension and content. Missing/
/// unreadable input yields an Unknown playlist with no tracks (never throws).
Playlist readFile(const std::filesystem::path& file);
/// Writes `tracks` in `format` to `file` atomically. Returns false for formats
/// with no writer (ASX/WPL/Unknown) or on IO error.
bool writeFile(const std::filesystem::path& file, const std::vector<Track>& tracks, Format format);

// --- autosave ---------------------------------------------------------------

struct AutosaveState {
    std::vector<Track> tracks;
    std::vector<std::size_t> queue;  ///< queue-order row indices
    int currentIndex = -1;           ///< playing row, -1 = none
    bool operator==(const AutosaveState&) const = default;
};

/// Autosave file names inside the state directory.
std::filesystem::path autosavePlaylistPath(const std::filesystem::path& dir);
std::filesystem::path autosaveStatePath(const std::filesystem::path& dir);

/// Persists the playlist (as a BOM'd M3U8) and the queue/index sidecar, both
/// atomically. A crash mid-write leaves the previous pair intact.
bool saveAutosave(const std::filesystem::path& dir, const AutosaveState& state);
/// Restores a previously saved state; nullopt when nothing valid is stored.
std::optional<AutosaveState> loadAutosave(const std::filesystem::path& dir);

}  // namespace koalaamp::playlist
