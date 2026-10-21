// SPDX-License-Identifier: MIT
// LI-1/LI-2 — scanner, incremental stamps, watcher, SQLite/FTS5 store.

#include "library/LibraryDb.hpp"
#include "library/Query.hpp"
#include "library/Scanner.hpp"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <sqlite3.h>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace koalaamp::library;

constexpr const char* kFixtureDir = KOALAAMP_LIBRARY_FIXTURE_DIR;

std::vector<ScannedTrack> scanFixtures() {
    std::vector<ScannedTrack> tracks;
    ScanCallbacks callbacks;
    callbacks.onTrack = [&](const ScannedTrack& track) { tracks.push_back(track); };
    const ScanSummary summary = scanRoots({fs::path(kFixtureDir)}, callbacks);
    CHECK(summary.added == tracks.size());
    return tracks;
}

const ScannedTrack* findTrack(const std::vector<ScannedTrack>& tracks, const std::string& name) {
    const auto it = std::find_if(tracks.begin(), tracks.end(), [&](const ScannedTrack& track) {
        return track.track.path.find(name) != std::string::npos;
    });
    return it != tracks.end() ? &*it : nullptr;
}

fs::path tempDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir, error);
    return dir;
}

} // namespace

TEST_CASE("the scanner reads tags, skips hidden and ignored trees") {
    const std::vector<ScannedTrack> tracks = scanFixtures();
    REQUIRE(tracks.size() == 4); // .hidden and ignored/ are skipped

    const ScannedTrack* flac = findTrack(tracks, "01-tagged.flac");
    REQUIRE(flac != nullptr);
    CHECK(flac->track.title == "Test Tone");
    CHECK(flac->track.artist == "Fixture Artist");
    CHECK(flac->track.albumArtist == "Fixture Artist");
    CHECK(flac->track.album == "Fixture Album");
    CHECK(flac->track.genre == "Test");
    CHECK(flac->track.year == 2024);
    CHECK(flac->track.trackNo == 3);
    CHECK(flac->track.discNo == 1);
    CHECK(flac->composer == "Fixture Composer");
    CHECK(flac->format == "flac");
    CHECK(flac->track.durationMs > 0);
    CHECK(flac->track.durationMs < 2000);
    REQUIRE(flac->track.rg.has_value());
    CHECK(std::abs(flac->track.rg->trackGain - (-6.5)) < 0.001);
    CHECK(std::abs(flac->track.rg->trackPeak - 0.8) < 0.001);
    CHECK(flac->artPath.empty()); // root has no folder art

    const ScannedTrack* mp3 = findTrack(tracks, "02-tagged.mp3");
    REQUIRE(mp3 != nullptr);
    CHECK(mp3->track.title == "Second Tone");
    CHECK(mp3->track.trackNo == 4);

    const ScannedTrack* wav = findTrack(tracks, "03-untagged.wav");
    REQUIRE(wav != nullptr);
    CHECK(wav->track.title == "03-untagged"); // filename fallback
    CHECK(wav->track.artist.empty());

    const ScannedTrack* ogg = findTrack(tracks, "04-tagged.ogg");
    REQUIRE(ogg != nullptr);
    CHECK(ogg->track.title == "Sub Tone");
    CHECK(ogg->artPath.find("folder.jpg") != std::string::npos);
}

TEST_CASE("incremental scans skip unchanged files and update touched ones") {
    const fs::path dir = tempDir("koalaamp-li1-incremental");
    fs::copy_file(fs::path(kFixtureDir) / "01-tagged.flac", dir / "track.flac");
    LibraryDb db{dir / "library.db"};
    REQUIRE(db.open());

    ScanCallbacks callbacks;
    std::atomic<int> emitted{0};
    callbacks.known = [&](const std::string& path) { return db.stamp(path); };
    callbacks.onTrack = [&](const ScannedTrack& track) {
        ++emitted;
        db.upsert(track);
    };
    ScanSummary summary = scanRoots({dir}, callbacks);
    CHECK(emitted.load() == 1);
    CHECK(summary.added == 1);

    summary = scanRoots({dir}, callbacks);
    CHECK(summary.unchanged == 1);
    CHECK(summary.added == 0);
    CHECK(emitted.load() == 1);

    // A new mtime is a change even when the bytes are identical.
    std::error_code error;
    fs::last_write_time(dir / "track.flac",
                        fs::file_time_type::clock::now() + std::chrono::seconds(5), error);
    REQUIRE_FALSE(error);
    summary = scanRoots({dir}, callbacks);
    CHECK(summary.updated == 1);
    CHECK(emitted.load() == 2);
    CHECK(db.count() == 1);

    fs::remove_all(dir, error);
}

TEST_CASE("the watcher reports a settled change") {
    const fs::path dir = tempDir("koalaamp-li1-watcher");
    fs::copy_file(fs::path(kFixtureDir) / "01-tagged.flac", dir / "first.flac");

    std::atomic<int> changes{0};
    LibraryWatcher watcher({dir}, std::chrono::milliseconds(200));
    watcher.setOnChange([&] { ++changes; });
    watcher.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(120)); // let the baseline settle

    fs::copy_file(fs::path(kFixtureDir) / "02-tagged.mp3", dir / "second.mp3");

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (changes.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    watcher.stop();
    CHECK(changes.load() >= 1);

    std::error_code error;
    fs::remove_all(dir, error);
}

TEST_CASE("the store round-trips, searches with FTS5 and browses albums") {
    const fs::path dir = tempDir("koalaamp-li2-db");
    const std::vector<ScannedTrack> scanned = scanFixtures();
    LibraryDb db{dir / "library.db"};
    REQUIRE(db.open());
    REQUIRE(db.begin());
    for (const ScannedTrack& track : scanned) {
        REQUIRE(db.upsert(track));
    }
    REQUIRE(db.commit());
    CHECK(db.count() == 4);

    const std::vector<Track> byArtist = db.search("fixture", 20);
    CHECK(byArtist.size() == 3); // the untagged WAV has no artist/album text

    const std::vector<Track> tones = db.search("tone", 20);
    CHECK(tones.size() == 3); // Test Tone, Second Tone, Sub Tone

    const std::vector<Track> quoted = db.search("\"Tone\"", 20);
    CHECK_FALSE(quoted.empty()); // punctuation is quoted, not parsed as syntax
    CHECK(db.search("   ", 20).empty());

    const std::vector<AlbumRow> albums = db.albums();
    REQUIRE(albums.size() == 2);
    const auto fixture = std::find_if(albums.begin(), albums.end(), [](const AlbumRow& row) {
        return row.name == "Fixture Album";
    });
    REQUIRE(fixture != albums.end());
    CHECK(fixture->albumArtist == "Fixture Artist");
    CHECK(fixture->year == 2024);
    CHECK(fixture->trackCount == 2);

    const std::vector<Track> albumTracks = db.albumTracks("Fixture Album", "Fixture Artist");
    REQUIRE(albumTracks.size() == 2);
    CHECK(albumTracks[0].trackNo == 3);
    CHECK(albumTracks[1].trackNo == 4); // disc/track order

    const std::vector<ArtistRow> artists = db.artists();
    REQUIRE(artists.size() == 1);
    CHECK(artists[0].name == "Fixture Artist");

    // Stamp lookup drives incremental scans.
    const auto stamp = db.stamp(albumTracks[0].path);
    REQUIRE(stamp.has_value());
    CHECK(stamp->size > 0);

    // Reopening the same file keeps rows and the FTS index.
    db.close();
    REQUIRE(db.open());
    CHECK(db.count() == 4);
    CHECK(db.search("tone", 20).size() == 3);

    REQUIRE(db.erase(albumTracks[0].path));
    CHECK(db.count() == 3);
    // "Test" is also the shared genre, so a title query would still match the
    // MP3; the composer is unique to the erased FLAC.
    CHECK(db.search("Fixture Composer", 20).empty());

    std::error_code error;
    fs::remove_all(dir, error);
}

TEST_CASE("100k rows stay within the search latency budget") {
    const fs::path dir = tempDir("koalaamp-li2-perf");
    const fs::path dbPath = dir / "library.db";
    LibraryDb db{dbPath};
    REQUIRE(db.open());
    REQUIRE(db.begin());

    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    constexpr int kRows = 100000;
    for (int i = 0; i < kRows; ++i) {
        ScannedTrack scanned;
        scanned.track.src = SourceId::Local;
        scanned.track.path = "/music/" + std::to_string(i) + ".flac";
        scanned.track.id = scanned.track.path;
        scanned.track.title = "Track " + std::to_string(i);
        scanned.track.artist = "Artist " + std::to_string(i % 100);
        scanned.track.albumArtist = scanned.track.artist;
        scanned.track.album = "Album " + std::to_string(i % 1000);
        scanned.track.genre = "Genre";
        scanned.track.trackNo = (i % 12) + 1;
        scanned.track.durationMs = 180000;
        scanned.mtime = (now - (i % 6) * 10 * 86400) * 1'000'000'000LL;
        scanned.format = "flac";
        REQUIRE(db.upsert(scanned));
    }
    REQUIRE(db.commit());
    CHECK(db.count() == kRows);

    std::vector<double> millis;
    for (int q = 0; q < 20; ++q) {
        const std::string text = "Track " + std::to_string(90000 + q * 100);
        const auto started = std::chrono::steady_clock::now();
        const std::vector<Track> hits = db.search(text, 50);
        millis.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count());
        CHECK_FALSE(hits.empty());
    }
    std::sort(millis.begin(), millis.end());
    const double p95 = millis[static_cast<std::size_t>(millis.size() * 0.95)];
    CHECK(p95 < 100.0);

    // LI-3 smart views on the same 100k rows. These are the queries the library
    // window actually sends (limit 5000, fixed orders).
    const struct {
        const char* text;
        const char* order;
        const char* name;
    } views[] = {
        {R"(genre = "Genre")", "default", "genre"},
        {R"(artist has "Artist 1")", "default", "artist"},
        {R"(album begins "Album 9")", "default", "album"},
        {R"(title has "Track 99")", "title", "title"},
    };
    for (const auto& view : views) {
        const auto parsed = parseQuery(view.text);
        REQUIRE(parsed.query.has_value());
        std::vector<double> viewMillis;
        for (int q = 0; q < 20; ++q) {
            const auto started = std::chrono::steady_clock::now();
            const auto rows = db.query(*parsed.query, 5000, view.order);
            viewMillis.push_back(std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - started)
                                     .count());
            CHECK_FALSE(rows.empty());
        }
        std::sort(viewMillis.begin(), viewMillis.end());
        const double viewP95 = viewMillis[static_cast<std::size_t>(viewMillis.size() * 0.95)];
        INFO("smart view " << view.name << " p95=" << viewP95 << "ms");
        CHECK(viewP95 < 100.0);
    }

    // Row-for-row reference check for the six smart views the library window
    // offers. The update gives the fixture deterministic dates/counters without
    // paying for 100k markPlayed() calls; the reference SQL below is written by
    // hand, not generated from Query.cpp.
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open_v2(dbPath.c_str(), &raw, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    const std::string update = "UPDATE tracks SET rating = (rowid % 6), playcount = (rowid % 7), "
                               "dateadded = " +
                               std::to_string(now) +
                               " - ((rowid % 6) * 10 * 86400), "
                               "lastplayed = CASE WHEN rowid % 5 = 0 THEN " +
                               std::to_string(now) + " - ((rowid % 2) * 30 * 86400) ELSE NULL END";
    char* sqlError = nullptr;
    REQUIRE(sqlite3_exec(raw, update.c_str(), nullptr, nullptr, &sqlError) == SQLITE_OK);
    if (sqlError != nullptr) {
        sqlite3_free(sqlError);
    }

    const std::int64_t threshold = now - 14 * 86400;
    struct ReferenceView {
        const char* text;
        const char* order;
        const char* where;
        bool bindThreshold;
    };
    const ReferenceView references[] = {
        {R"(dateadded > [2 weeks ago])", "recent", "dateadded > ?", true},
        {R"(lastplay > [2 weeks ago])", "played", "lastplayed > ?", true},
        {R"(lastupd > [2 weeks ago])", "modified", "(mtime / 1000000000) > ?", true},
        {R"(playcount > 0)", "played", "playcount > 0", false},
        {R"(playcount = 0)", "title", "playcount = 0", false},
        {R"(rating > 0)", "rating", "rating > 0", false},
    };
    for (const ReferenceView& reference : references) {
        const auto parsed = parseQuery(reference.text, now);
        REQUIRE(parsed.query.has_value());
        const auto actual = db.query(*parsed.query, 5000, reference.order);

        const std::string orderBy =
            std::string(reference.order) == "recent"
                ? "dateadded DESC, artist COLLATE NOCASE, title"
                : (std::string(reference.order) == "played"
                       ? "playcount DESC, lastplayed DESC, artist COLLATE NOCASE, title"
                       : (std::string(reference.order) == "rating"
                              ? "rating DESC, artist COLLATE NOCASE, title"
                              : (std::string(reference.order) == "modified"
                                     ? "mtime DESC, artist COLLATE NOCASE, title"
                                     : "title COLLATE NOCASE, artist COLLATE NOCASE")));
        const std::string sql = std::string("SELECT path FROM tracks WHERE ") + reference.where +
                                " ORDER BY " + orderBy + " LIMIT 5000";
        sqlite3_stmt* statement = nullptr;
        REQUIRE(sqlite3_prepare_v2(raw, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK);
        if (reference.bindThreshold) {
            sqlite3_bind_int64(statement, 1, threshold);
        }
        std::vector<std::string> expected;
        while (sqlite3_step(statement) == SQLITE_ROW) {
            expected.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)));
        }
        sqlite3_finalize(statement);

        INFO("reference view " << reference.text << " rows=" << expected.size());
        REQUIRE(actual.size() == expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i) {
            INFO("row " << i << " actual=" << actual[i].path << " expected=" << expected[i]);
            CHECK(actual[i].path == expected[i]);
        }
    }
    sqlite3_close(raw);

    // Artist expansion and artist-track loading use the same 100k rows.
    std::vector<double> expandMillis;
    for (int q = 0; q < 20; ++q) {
        const std::string artist = "Artist " + std::to_string(q % 100);
        const auto started = std::chrono::steady_clock::now();
        const auto albumsForArtist = db.albumsForArtist(artist);
        const auto tracks = db.artistTracks(artist, 5000);
        expandMillis.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count());
        CHECK_FALSE(albumsForArtist.empty());
        CHECK_FALSE(tracks.empty());
    }
    std::sort(expandMillis.begin(), expandMillis.end());
    const double expandP95 = expandMillis[static_cast<std::size_t>(expandMillis.size() * 0.95)];
    INFO("artist expand p95=" << expandP95 << "ms");
    CHECK(expandP95 < 100.0);

    std::error_code error;
    fs::remove_all(dir, error);
}

TEST_CASE("smart-view grammar parses fields, operators and relative dates") {
    using koalaamp::library::parseQuery;
    using koalaamp::library::QueryParam;
    constexpr std::int64_t kNow = 1'800'000'000;

    {
        const auto result = parseQuery(R"(artist has "Fixture" && rating > 3)", kNow);
        REQUIRE(result.query.has_value());
        CHECK(result.query->where.find("LIKE") != std::string::npos);
        REQUIRE(result.query->params.size() == 2);
        CHECK(result.query->params[0].text == "%Fixture%");
        CHECK(result.query->params[1].integer == 3);
    }
    {
        const auto result = parseQuery(R"(genre = "Rock" || (year <= 2000 && !ISEMPTY))", kNow);
        CHECK_FALSE(result.query.has_value()); // ISEMPTY needs its field
        CHECK(!result.error.empty());
    }
    {
        const auto result = parseQuery(R"(genre = "Rock" || (year <= 2000 && rating > 1))", kNow);
        REQUIRE(result.query.has_value());
        CHECK(result.query->where.find(" OR ") != std::string::npos);
        CHECK(result.query->where.find(" AND ") != std::string::npos);
    }
    {
        const auto result = parseQuery("lastplay > [1 week ago]", kNow);
        REQUIRE(result.query.has_value());
        REQUIRE(result.query->params.size() == 1);
        CHECK(result.query->params[0].integer == kNow - 7 * 24 * 60 * 60);
    }
    {
        const auto result = parseQuery("lastupd > [2 weeks ago]", kNow);
        REQUIRE(result.query.has_value());
        CHECK(result.query->where.find("(mtime / 1000000000)") != std::string::npos);
        REQUIRE(result.query->params.size() == 1);
        CHECK(result.query->params[0].integer == kNow - 14 * 24 * 60 * 60);
    }
    {
        const auto result = parseQuery("title begins \"Test\"", kNow);
        REQUIRE(result.query.has_value());
        REQUIRE(result.query->params.size() == 1);
        CHECK(result.query->params[0].text == "Test%");
    }
    {
        const auto result = parseQuery("album ends \"Album\"", kNow);
        REQUIRE(result.query.has_value());
        REQUIRE(result.query->params.size() == 1);
        CHECK(result.query->params[0].text == "%Album");
    }
    {
        const auto result = parseQuery("album nothas \"Live\"", kNow);
        REQUIRE(result.query.has_value());
        CHECK(result.query->where.find("NOT LIKE") != std::string::npos);
    }
    {
        const auto result = parseQuery(R"(1 && genre = "Rock")", kNow);
        REQUIRE(result.query.has_value());
        CHECK(result.query->where.find("1 AND") != std::string::npos);
    }
    {
        const auto result = parseQuery("length > 200", kNow);
        REQUIRE(result.query.has_value());
        REQUIRE(result.query->params.size() == 1);
        CHECK(result.query->params[0].integer == 200000); // seconds -> ms
    }
    {
        const auto result = parseQuery("artist isempty", kNow);
        REQUIRE(result.query.has_value());
        CHECK(result.query->where.find("IS NULL") != std::string::npos);
    }
    {
        const auto result = parseQuery("bogus = 1", kNow);
        CHECK_FALSE(result.query.has_value());
        CHECK(!result.error.empty());
    }
    {
        const auto result = parseQuery("artist =", kNow);
        CHECK_FALSE(result.query.has_value());
    }
    {
        const auto result = parseQuery("artist = \"unterminated", kNow);
        CHECK_FALSE(result.query.has_value());
    }
    {
        const auto result = parseQuery("artist = \"A\" junk", kNow);
        CHECK_FALSE(result.query.has_value());
    }
}

TEST_CASE("library queries, ratings and play counts flow through SQLite") {
    const std::vector<ScannedTrack> scanned = scanFixtures();
    const fs::path dir = tempDir("koalaamp-li3-query");
    {
        LibraryDb db{dir / "library.db"};
        REQUIRE(db.open());
        REQUIRE(db.begin());
        for (const ScannedTrack& track : scanned) {
            REQUIRE(db.upsert(track));
        }
        REQUIRE(db.commit());

        const auto byArtist = parseQuery(R"(artist has "Fixture")");
        REQUIRE(byArtist.query.has_value());
        const std::vector<Track> rows = db.query(*byArtist.query);
        CHECK(rows.size() == 3);

        const auto rated = parseQuery("rating >= 1");
        REQUIRE(rated.query.has_value());
        CHECK(db.query(*rated.query).empty());

        const auto fixture = std::find_if(rows.begin(), rows.end(), [](const Track& track) {
            return track.title == "Test Tone";
        });
        REQUIRE(fixture != rows.end());
        REQUIRE(db.setRating(fixture->path, 4));
        const std::vector<Track> ratedNow = db.query(*rated.query);
        REQUIRE(ratedNow.size() == 1);
        CHECK(ratedNow[0].rating == 4);

        REQUIRE(db.markPlayed(fixture->path, 1'800'000'000));
        const std::vector<Track> played = db.query(*rated.query);
        REQUIRE(played.size() == 1);
        CHECK(played[0].playCount == 1);
        CHECK(played[0].lastPlayed == 1'800'000'000);
        const auto liveHistory = db.history();
        REQUIRE(liveHistory.size() == 1);
        CHECK(liveHistory[0].path == fixture->path);
        CHECK(liveHistory[0].playedAt == 1'800'000'000);
        CHECK(liveHistory[0].source == "local");

        const std::vector<std::string> genres = db.genres();
        CHECK(std::find(genres.begin(), genres.end(), "Test") != genres.end());
        CHECK_FALSE(db.years().empty());
        CHECK(db.albumsForArtist("Fixture Artist").size() == 2);
        CHECK(db.artistTracks("Fixture Artist").size() == 3);
    }
    std::error_code error;
    fs::remove_all(dir, error);
}

TEST_CASE("Winamp migration stats apply idempotently by path") {
    const fs::path dir = tempDir("koalaamp-mi1-apply");
    LibraryDb db{dir / "library.db"};
    REQUIRE(db.open());
    ScannedTrack scanned;
    scanned.track.path = "/Music/Migrated.flac";
    scanned.track.title = "Migrated";
    scanned.track.artist = "Artist";
    scanned.format = "flac";
    scanned.mtime = 1;
    scanned.size = 10;
    REQUIRE(db.upsert(scanned));

    MigrationStats stats;
    stats.rating = 4;
    stats.playCount = 27;
    stats.lastPlayed = 1'700'000'000;
    stats.dateAdded = 1'690'000'000;
    stats.albumGain = -1.5;
    stats.trackGain = 2.5;
    stats.bpm = 120;
    stats.year = 2020;
    stats.trackNo = 3;
    stats.discNo = 1;
    stats.genre = "Rock";
    stats.comment = "Imported";
    stats.albumArtist = "Album Artist";
    stats.durationMs = 215000;
    stats.bitrate = 320000;
    REQUIRE(db.applyMigration("/Music/Migrated.flac", stats));
    const auto first = db.allTracks();
    REQUIRE(first.size() == 1);
    CHECK(first[0].rating == 4);
    CHECK(first[0].playCount == 27);
    CHECK(first[0].lastPlayed == 1'700'000'000);
    CHECK(first[0].dateAdded == 1'690'000'000);
    CHECK(first[0].bpm == 120);
    CHECK(first[0].genre == "Rock");
    CHECK(first[0].comment == "Imported");
    CHECK(first[0].albumArtist == "Album Artist");
    CHECK(first[0].durationMs == 215000);
    CHECK(first[0].bitrate == 320000);

    // A case-different Winamp path still matches; repeated application is stable.
    REQUIRE(db.applyMigration("/music/migrated.flac", stats));
    const auto second = db.allTracks();
    REQUIRE(second.size() == 1);
    CHECK(second[0].rating == first[0].rating);
    CHECK(second[0].playCount == first[0].playCount);
    CHECK(second[0].lastPlayed == first[0].lastPlayed);
    CHECK(second[0].dateAdded == first[0].dateAdded);
    CHECK(second[0].genre == first[0].genre);

    CHECK_FALSE(db.applyMigration("/music/missing.flac", stats));
    std::error_code error;
    fs::remove_all(dir, error);
}

TEST_CASE("play history is newest-first and idempotent by path and timestamp") {
    const fs::path dir = tempDir("koalaamp-mi1-history");
    LibraryDb db{dir / "library.db"};
    REQUIRE(db.open());

    PlayHistoryEntry first;
    first.path = "/Music/first.flac";
    first.title = "First";
    first.artist = "Artist";
    first.album = "Album";
    first.playedAt = 1'700'000'000;
    REQUIRE(db.addHistory(first));
    CHECK_FALSE(db.addHistory(first)); // same event: unique key

    PlayHistoryEntry second;
    second.path = "/Music/second.flac";
    second.title = "Second";
    second.playedAt = 1'700'000'100;
    REQUIRE(db.addHistory(second));

    PlayHistoryEntry samePathLater;
    samePathLater.path = first.path;
    samePathLater.title = "First again";
    samePathLater.playedAt = 1'700'000'200;
    REQUIRE(db.addHistory(samePathLater));

    CHECK(db.historyCount() == 3);
    const auto history = db.history();
    REQUIRE(history.size() == 3);
    CHECK(history[0].path == "/Music/first.flac");
    CHECK(history[0].playedAt == 1'700'000'200);
    CHECK(history[1].path == "/Music/second.flac");
    CHECK(history[2].path == "/Music/first.flac");

    const auto page = db.history(1, 1);
    REQUIRE(page.size() == 1);
    CHECK(page[0].path == "/Music/second.flac");

    CHECK_FALSE(db.addHistory(PlayHistoryEntry{}));
    std::error_code error;
    fs::remove_all(dir, error);
}

TEST_CASE("opening a v1 library adds the history table") {
    const fs::path dir = tempDir("koalaamp-mi1-history-upgrade");
    const fs::path database = dir / "library.db";
    {
        LibraryDb db{database};
        REQUIRE(db.open());
    }
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(database.string().c_str(), &raw) == SQLITE_OK);
    REQUIRE(sqlite3_exec(raw, "DROP TABLE play_history; PRAGMA user_version = 1;", nullptr, nullptr,
                         nullptr) == SQLITE_OK);
    sqlite3_close(raw);

    LibraryDb db{database};
    REQUIRE(db.open());
    PlayHistoryEntry entry;
    entry.path = "/Music/upgraded.flac";
    entry.playedAt = 1'700'000'000;
    REQUIRE(db.addHistory(entry));
    CHECK(db.historyCount() == 1);
    std::error_code error;
    fs::remove_all(dir, error);
}

TEST_CASE("backfill seeds history from an existing lastplayed value") {
    const fs::path dir = tempDir("koalaamp-mi1-backfill");
    LibraryDb db{dir / "library.db"};
    REQUIRE(db.open());
    ScannedTrack scanned;
    scanned.track.path = "/Music/legacy.flac";
    scanned.track.title = "Legacy";
    scanned.track.artist = "Artist";
    scanned.format = "flac";
    scanned.mtime = 1;
    scanned.size = 10;
    REQUIRE(db.upsert(scanned));

    MigrationStats stats;
    stats.lastPlayed = 1'700'000'000;
    REQUIRE(db.applyMigration("/Music/legacy.flac", stats));
    CHECK(db.historyCount() == 0);

    CHECK(db.backfillHistoryFromTracks() == 1);
    CHECK(db.backfillHistoryFromTracks() == 0);
    const auto history = db.history();
    REQUIRE(history.size() == 1);
    CHECK(history[0].path == "/Music/legacy.flac");
    CHECK(history[0].playedAt == 1'700'000'000);
    CHECK(history[0].source == "local");
    std::error_code error;
    fs::remove_all(dir, error);
}
