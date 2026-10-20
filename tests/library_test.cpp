// SPDX-License-Identifier: MIT
// LI-1/LI-2 — scanner, incremental stamps, watcher, SQLite/FTS5 store.

#include "library/LibraryDb.hpp"
#include "library/Scanner.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
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
        millis.push_back(std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - started)
                             .count());
        CHECK_FALSE(hits.empty());
    }
    std::sort(millis.begin(), millis.end());
    const double p95 = millis[static_cast<std::size_t>(millis.size() * 0.95)];
    CHECK(p95 < 100.0);

    std::error_code error;
    fs::remove_all(dir, error);
}
