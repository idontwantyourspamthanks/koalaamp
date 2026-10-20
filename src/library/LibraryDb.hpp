// SPDX-License-Identifier: MIT
// library — SQLite + FTS5 library store (LI-2).
//
// Schema per spec §7.2. One connection, single-threaded by contract (the scan
// worker writes, the UI reads after a scan or on demand). Failures are reported
// through `lastError()`, never exceptions.

#pragma once

#include "library/Query.hpp"
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

/// Winamp migration fields (MI-1). Numeric fields at their sentinel mean "leave
/// the existing value alone"; ratings 0..5 and playcount 0 are authoritative.
struct MigrationStats {
    int rating{-1};
    int playCount{-1};
    std::int64_t lastPlayed{-1};
    std::int64_t dateAdded{-1};
    double albumGain{0.0};
    double trackGain{0.0};
    int bpm{-1};
    int year{-1};
    int trackNo{-1};
    int discNo{-1};
    std::string genre;
    std::string comment;
    std::string albumArtist;
    std::int64_t durationMs{-1};
    std::int64_t bitrate{-1};
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
    std::vector<AlbumRow> albumsForArtist(std::string_view artist, int limit = 0);
    std::vector<Track> albumTracks(std::string_view album, std::string_view albumArtist);
    std::vector<Track> artistTracks(std::string_view artist, int limit = 0);
    std::vector<Track> allTracks(int limit = 0);

    /// Smart-view query. An empty `order` uses artist/album/disc/track order.
    std::vector<Track> query(const ParsedQuery& query, int limit = 0, std::string_view order = {});

    std::vector<std::string> genres();
    std::vector<int> years();

    /// Writes the user's 0..5 star rating. Returns false when the path is absent.
    bool setRating(std::string_view path, int stars);
    /// Increments playcount and stamps lastplayed. Returns false when absent.
    bool markPlayed(std::string_view path, std::int64_t unixSeconds);

    /// Applies Winamp NDE values by path (case-insensitive fallback). Idempotent:
    /// repeating the same migration leaves the row identical. Returns false when
    /// no library row matches.
    bool applyMigration(std::string_view path, const MigrationStats& stats);

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
