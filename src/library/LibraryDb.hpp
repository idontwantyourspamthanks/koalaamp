// SPDX-License-Identifier: MIT
// library — SQLite + FTS5 library store (LI-2).
//
// Schema per spec §7.2. One connection, single-threaded by contract (the scan
// worker writes, the UI reads after a scan or on demand). Failures are reported
// through `lastError()`, never exceptions.

#pragma once

#include "library/Scanner.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace koalaamp::library {

struct AlbumRow {
    std::string name;
    std::string albumArtist;
    int year{0};
    int trackCount{0};
};

struct ArtistRow {
    std::string name;
    int trackCount{0};
};

class LibraryDb {
public:
    explicit LibraryDb(std::filesystem::path file);
    ~LibraryDb();
    LibraryDb(const LibraryDb&) = delete;
    LibraryDb& operator=(const LibraryDb&) = delete;

    /// Opens (creating if needed) and migrates to schema v1.
    bool open();
    void close();
    bool ready() const { return db_ != nullptr; }
    const std::string& lastError() const { return lastError_; }

    bool upsert(const ScannedTrack& track);
    std::optional<FileStamp> stamp(std::string_view path);
    bool erase(std::string_view path);
    std::size_t count();

    /// FTS5 prefix search over title/artist/album/album_artist/genre/composer.
    /// Tokens are ANDed; punctuation is quoted, never interpreted as syntax.
    std::vector<Track> search(std::string_view text, int limit = 100);

    std::vector<AlbumRow> albums(int limit = 0);
    std::vector<ArtistRow> artists(int limit = 0);
    std::vector<Track> albumTracks(std::string_view album, std::string_view albumArtist);

    /// Batch a scan: begin/commit around many `upsert` calls.
    bool begin();
    bool commit();
    bool rollback();

private:
    bool migrate();
    bool prepareUpsert();
    bool exec(const char* sql);
    void setError(const char* context);

    std::filesystem::path file_;
    sqlite3* db_{nullptr};
    sqlite3_stmt* upsert_{nullptr};
    std::string lastError_;
};

} // namespace koalaamp::library
