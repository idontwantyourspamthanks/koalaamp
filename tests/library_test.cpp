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
#include <filesystem>
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
    LibraryDb db{dir / "library.db"};
    REQUIRE(db.open());
    REQUIRE(db.begin());

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

        const std::vector<std::string> genres = db.genres();
        CHECK(std::find(genres.begin(), genres.end(), "Test") != genres.end());
        CHECK_FALSE(db.years().empty());
        CHECK(db.albumsForArtist("Fixture Artist").size() == 2);
        CHECK(db.artistTracks("Fixture Artist").size() == 3);
    }
    std::error_code error;
    fs::remove_all(dir, error);
}
