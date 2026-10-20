// SPDX-License-Identifier: MIT
// library — SQLite + FTS5 store. See LibraryDb.hpp.

#include "library/LibraryDb.hpp"

#include <sqlite3.h>

#include <cctype>
#include <ctime>
#include <utility>

namespace koalaamp::library {
namespace {

constexpr const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS tracks(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT UNIQUE NOT NULL,
    mtime INTEGER NOT NULL,
    size INTEGER NOT NULL,
    format TEXT,
    samplerate INTEGER,
    channels INTEGER,
    bitrate INTEGER,
    duration_ms INTEGER,
    title TEXT,
    artist TEXT,
    album_artist TEXT,
    album TEXT,
    year INTEGER,
    genre TEXT,
    trackno INTEGER,
    discno INTEGER,
    composer TEXT,
    publisher TEXT,
    comment TEXT,
    bpm INTEGER,
    rating INTEGER DEFAULT 0,
    playcount INTEGER DEFAULT 0,
    lastplayed INTEGER,
    dateadded INTEGER,
    rg_track_gain REAL,
    rg_track_peak REAL,
    rg_album_gain REAL,
    rg_album_peak REAL,
    source TEXT DEFAULT 'local',
    remote_id TEXT,
    art_cache_path TEXT
);
CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5(
    title, artist, album, album_artist, genre, composer,
    content='tracks', content_rowid='id'
);
CREATE TRIGGER IF NOT EXISTS tracks_ai AFTER INSERT ON tracks BEGIN
    INSERT INTO tracks_fts(rowid, title, artist, album, album_artist, genre, composer)
    VALUES (new.id, new.title, new.artist, new.album, new.album_artist, new.genre, new.composer);
END;
CREATE TRIGGER IF NOT EXISTS tracks_ad AFTER DELETE ON tracks BEGIN
    INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, album_artist, genre, composer)
    VALUES ('delete', old.id, old.title, old.artist, old.album, old.album_artist, old.genre,
            old.composer);
END;
CREATE TRIGGER IF NOT EXISTS tracks_au AFTER UPDATE ON tracks BEGIN
    INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, album_artist, genre, composer)
    VALUES ('delete', old.id, old.title, old.artist, old.album, old.album_artist, old.genre,
            old.composer);
    INSERT INTO tracks_fts(rowid, title, artist, album, album_artist, genre, composer)
    VALUES (new.id, new.title, new.artist, new.album, new.album_artist, new.genre, new.composer);
END;
)sql";

constexpr const char* kUpsert = R"sql(
INSERT INTO tracks(
    path, mtime, size, format, samplerate, channels, bitrate, duration_ms,
    title, artist, album_artist, album, year, genre, trackno, discno,
    composer, publisher, comment, bpm, rating, playcount, lastplayed, dateadded,
    rg_track_gain, rg_track_peak, rg_album_gain, rg_album_peak,
    source, remote_id, art_cache_path)
VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
        0, 0, NULL, ?, ?, ?, ?, ?, 'local', NULL, ?)
ON CONFLICT(path) DO UPDATE SET
    mtime=excluded.mtime, size=excluded.size, format=excluded.format,
    samplerate=excluded.samplerate, channels=excluded.channels, bitrate=excluded.bitrate,
    duration_ms=excluded.duration_ms, title=excluded.title, artist=excluded.artist,
    album_artist=excluded.album_artist, album=excluded.album, year=excluded.year,
    genre=excluded.genre, trackno=excluded.trackno, discno=excluded.discno,
    composer=excluded.composer, publisher=excluded.publisher, comment=excluded.comment,
    bpm=excluded.bpm, rg_track_gain=excluded.rg_track_gain,
    rg_track_peak=excluded.rg_track_peak, rg_album_gain=excluded.rg_album_gain,
    rg_album_peak=excluded.rg_album_peak, art_cache_path=excluded.art_cache_path
)sql";

void bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), static_cast<int>(value.size()),
                      SQLITE_TRANSIENT);
}

std::string columnText(sqlite3_stmt* statement, int index) {
    const unsigned char* text = sqlite3_column_text(statement, index);
    return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string{};
}

Track rowToTrack(sqlite3_stmt* statement) {
    Track track;
    track.src = SourceId::Local;
    track.path = columnText(statement, 0);
    track.id = track.path;
    track.title = columnText(statement, 1);
    track.artist = columnText(statement, 2);
    track.album = columnText(statement, 3);
    track.albumArtist = columnText(statement, 4);
    track.genre = columnText(statement, 5);
    track.year = sqlite3_column_int(statement, 6);
    track.trackNo = sqlite3_column_int(statement, 7);
    track.discNo = sqlite3_column_int(statement, 8);
    track.durationMs = sqlite3_column_int64(statement, 9);
    track.bitrate = sqlite3_column_int64(statement, 10);
    return track;
}

std::string ftsQuery(std::string_view text) {
    std::string query;
    std::size_t begin = 0;
    while (begin < text.size()) {
        while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
            ++begin;
        }
        std::size_t end = begin;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        if (end > begin) {
            if (!query.empty()) {
                query.push_back(' ');
            }
            query.push_back('"');
            for (std::size_t i = begin; i < end; ++i) {
                if (text[i] == '"') {
                    query.push_back('"'); // FTS5 escapes a quote by doubling it
                }
                query.push_back(text[i]);
            }
            query += "\"*"; // prefix match on the last token shape
        }
        begin = end;
    }
    return query;
}

} // namespace

LibraryDb::LibraryDb(std::filesystem::path file) : file_(std::move(file)) {}

LibraryDb::~LibraryDb() {
    close();
}

bool LibraryDb::open() {
    close();
    std::error_code error;
    if (!file_.parent_path().empty()) {
        std::filesystem::create_directories(file_.parent_path(), error);
    }
    if (sqlite3_open_v2(file_.string().c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        setError("open");
        close();
        return false;
    }
    if (!exec("PRAGMA journal_mode=WAL") || !exec("PRAGMA synchronous=NORMAL") ||
        !exec("PRAGMA foreign_keys=ON")) {
        close();
        return false;
    }
    if (!migrate() || !prepareUpsert()) {
        close();
        return false;
    }
    return true;
}

void LibraryDb::close() {
    if (upsert_ != nullptr) {
        sqlite3_finalize(upsert_);
        upsert_ = nullptr;
    }
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void LibraryDb::setError(const char* context) {
    lastError_ = std::string(context) + ": " +
                 (db_ != nullptr ? sqlite3_errmsg(db_) : "no database");
}

bool LibraryDb::exec(const char* sql) {
    char* message = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        lastError_ = std::string(message != nullptr ? message : "sqlite error");
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool LibraryDb::migrate() {
    sqlite3_stmt* versionStatement = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA user_version", -1, &versionStatement, nullptr) !=
        SQLITE_OK) {
        setError("user_version");
        return false;
    }
    int version = 0;
    if (sqlite3_step(versionStatement) == SQLITE_ROW) {
        version = sqlite3_column_int(versionStatement, 0);
    }
    sqlite3_finalize(versionStatement);

    if (version == 0) {
        if (!exec("BEGIN")) {
            return false;
        }
        if (!exec(kSchema) || !exec("PRAGMA user_version = 1") || !exec("COMMIT")) {
            exec("ROLLBACK");
            return false;
        }
        return true;
    }
    if (version != 1) {
        lastError_ = "unsupported library schema version " + std::to_string(version);
        return false;
    }
    return true;
}

bool LibraryDb::prepareUpsert() {
    return sqlite3_prepare_v2(db_, kUpsert, -1, &upsert_, nullptr) == SQLITE_OK ||
           (setError("prepare upsert"), false);
}

bool LibraryDb::upsert(const ScannedTrack& scanned) {
    if (db_ == nullptr || upsert_ == nullptr) {
        return false;
    }
    sqlite3_reset(upsert_);
    sqlite3_clear_bindings(upsert_);
    const Track& track = scanned.track;

    int index = 1;
    bindText(upsert_, index++, track.path);
    sqlite3_bind_int64(upsert_, index++, scanned.mtime);
    sqlite3_bind_int64(upsert_, index++, static_cast<sqlite3_int64>(scanned.size));
    bindText(upsert_, index++, scanned.format);
    sqlite3_bind_int(upsert_, index++, scanned.sampleRate);
    sqlite3_bind_int(upsert_, index++, scanned.channels);
    sqlite3_bind_int64(upsert_, index++, track.bitrate);
    sqlite3_bind_int64(upsert_, index++, track.durationMs);
    bindText(upsert_, index++, track.title);
    bindText(upsert_, index++, track.artist);
    bindText(upsert_, index++, track.albumArtist);
    bindText(upsert_, index++, track.album);
    sqlite3_bind_int(upsert_, index++, track.year);
    bindText(upsert_, index++, track.genre);
    sqlite3_bind_int(upsert_, index++, track.trackNo);
    sqlite3_bind_int(upsert_, index++, track.discNo);
    bindText(upsert_, index++, scanned.composer);
    bindText(upsert_, index++, scanned.publisher);
    bindText(upsert_, index++, scanned.comment);
    sqlite3_bind_int(upsert_, index++, scanned.bpm);
    sqlite3_bind_int64(upsert_, index++, static_cast<sqlite3_int64>(std::time(nullptr)));
    if (track.rg.has_value()) {
        sqlite3_bind_double(upsert_, index++, track.rg->trackGain);
        sqlite3_bind_double(upsert_, index++, track.rg->trackPeak);
        sqlite3_bind_double(upsert_, index++, track.rg->albumGain);
        sqlite3_bind_double(upsert_, index++, track.rg->albumPeak);
    } else {
        sqlite3_bind_null(upsert_, index++);
        sqlite3_bind_null(upsert_, index++);
        sqlite3_bind_null(upsert_, index++);
        sqlite3_bind_null(upsert_, index++);
    }
    bindText(upsert_, index++, scanned.artPath);

    if (sqlite3_step(upsert_) != SQLITE_DONE) {
        setError("upsert");
        return false;
    }
    return true;
}

std::optional<FileStamp> LibraryDb::stamp(std::string_view path) {
    if (db_ == nullptr) {
        return std::nullopt;
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT mtime, size FROM tracks WHERE path = ?", -1, &statement,
                           nullptr) != SQLITE_OK) {
        setError("stamp");
        return std::nullopt;
    }
    sqlite3_bind_text(statement, 1, path.data(), static_cast<int>(path.size()), SQLITE_TRANSIENT);
    std::optional<FileStamp> result;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        result = FileStamp{sqlite3_column_int64(statement, 0),
                           static_cast<std::uint64_t>(sqlite3_column_int64(statement, 1))};
    }
    sqlite3_finalize(statement);
    return result;
}

bool LibraryDb::erase(std::string_view path) {
    if (db_ == nullptr) {
        return false;
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "DELETE FROM tracks WHERE path = ?", -1, &statement, nullptr) !=
        SQLITE_OK) {
        setError("erase");
        return false;
    }
    sqlite3_bind_text(statement, 1, path.data(), static_cast<int>(path.size()), SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    if (!ok) {
        setError("erase");
    }
    sqlite3_finalize(statement);
    return ok;
}

std::size_t LibraryDb::count() {
    if (db_ == nullptr) {
        return 0;
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM tracks", -1, &statement, nullptr) !=
        SQLITE_OK) {
        return 0;
    }
    std::size_t total = 0;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        total = static_cast<std::size_t>(sqlite3_column_int64(statement, 0));
    }
    sqlite3_finalize(statement);
    return total;
}

std::vector<Track> LibraryDb::search(std::string_view text, int limit) {
    std::vector<Track> tracks;
    if (db_ == nullptr || limit <= 0) {
        return tracks;
    }
    const std::string match = ftsQuery(text);
    if (match.empty()) {
        return tracks;
    }
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT t.path, t.title, t.artist, t.album, t.album_artist, t.genre, t.year, "
        "t.trackno, t.discno, t.duration_ms, t.bitrate "
        "FROM tracks_fts f JOIN tracks t ON t.id = f.rowid "
        "WHERE tracks_fts MATCH ? ORDER BY rank LIMIT ?";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        setError("search");
        return tracks;
    }
    bindText(statement, 1, match);
    sqlite3_bind_int(statement, 2, limit);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        tracks.push_back(rowToTrack(statement));
    }
    sqlite3_finalize(statement);
    return tracks;
}

std::vector<AlbumRow> LibraryDb::albums(int limit) {
    std::vector<AlbumRow> rows;
    if (db_ == nullptr) {
        return rows;
    }
    std::string sql =
        "SELECT album, album_artist, MAX(year), COUNT(*) FROM tracks "
        "WHERE album <> '' GROUP BY album, album_artist ORDER BY album COLLATE NOCASE";
    if (limit > 0) {
        sql += " LIMIT ?";
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
        setError("albums");
        return rows;
    }
    if (limit > 0) {
        sqlite3_bind_int(statement, 1, limit);
    }
    while (sqlite3_step(statement) == SQLITE_ROW) {
        rows.push_back(AlbumRow{columnText(statement, 0), columnText(statement, 1),
                                sqlite3_column_int(statement, 2),
                                sqlite3_column_int(statement, 3)});
    }
    sqlite3_finalize(statement);
    return rows;
}

std::vector<ArtistRow> LibraryDb::artists(int limit) {
    std::vector<ArtistRow> rows;
    if (db_ == nullptr) {
        return rows;
    }
    std::string sql =
        "SELECT artist, COUNT(*) FROM tracks WHERE artist <> '' "
        "GROUP BY artist ORDER BY artist COLLATE NOCASE";
    if (limit > 0) {
        sql += " LIMIT ?";
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
        setError("artists");
        return rows;
    }
    if (limit > 0) {
        sqlite3_bind_int(statement, 1, limit);
    }
    while (sqlite3_step(statement) == SQLITE_ROW) {
        rows.push_back(ArtistRow{columnText(statement, 0), sqlite3_column_int(statement, 1)});
    }
    sqlite3_finalize(statement);
    return rows;
}

std::vector<Track> LibraryDb::albumTracks(std::string_view album, std::string_view albumArtist) {
    std::vector<Track> tracks;
    if (db_ == nullptr) {
        return tracks;
    }
    const char* sql =
        "SELECT path, title, artist, album, album_artist, genre, year, trackno, discno, "
        "duration_ms, bitrate FROM tracks WHERE album = ? AND album_artist = ? "
        // Albums without disc tags (discno 0) are single-disc: sort them with
        // disc 1 instead of ahead of every disc.
        "ORDER BY CASE WHEN discno > 0 THEN discno ELSE 1 END, trackno, path";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        setError("albumTracks");
        return tracks;
    }
    sqlite3_bind_text(statement, 1, album.data(), static_cast<int>(album.size()),
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, albumArtist.data(), static_cast<int>(albumArtist.size()),
                      SQLITE_TRANSIENT);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        tracks.push_back(rowToTrack(statement));
    }
    sqlite3_finalize(statement);
    return tracks;
}

bool LibraryDb::begin() {
    return exec("BEGIN");
}
bool LibraryDb::commit() {
    return exec("COMMIT");
}
bool LibraryDb::rollback() {
    return exec("ROLLBACK");
}

} // namespace koalaamp::library
