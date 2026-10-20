// SPDX-License-Identifier: MIT
// MI-1 — generated NDE data/index fixtures for the read-only reader.

#include "library/LibraryDb.hpp"
#include "migration/Migration.hpp"
#include "migration/NDE.hpp"
#include "migration/WinampConfig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace koalaamp::migration;

struct FieldSpec {
    int column;
    NdeType type;
    std::string text;
    std::int64_t number{0};
    float real{0.0F};
};

void put16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
}

void put32(std::vector<unsigned char>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xff));
    }
}

void put64(std::vector<unsigned char>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xff));
    }
}

struct BuiltNde {
    std::vector<unsigned char> data;
    std::vector<unsigned char> index;
};

BuiltNde buildNde(const std::vector<std::vector<FieldSpec>>& records) {
    BuiltNde built;
    built.data.insert(built.data.end(), {'N', 'D', 'E', 'T', 'A', 'B', 'L', 'E'});
    std::vector<std::uint32_t> offsets;
    for (const auto& fields : records) {
        offsets.push_back(static_cast<std::uint32_t>(built.data.size()));
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const FieldSpec& field = fields[i];
            std::vector<unsigned char> payload;
            if (field.type == NdeType::String) {
                put16(payload, static_cast<std::uint16_t>(field.text.size()));
                payload.insert(payload.end(), field.text.begin(), field.text.end());
            } else if (field.type == NdeType::Integer || field.type == NdeType::DateTime ||
                       field.type == NdeType::Length) {
                put32(payload, static_cast<std::uint32_t>(field.number));
            } else if (field.type == NdeType::Boolean) {
                payload.push_back(field.number != 0 ? 1 : 0);
            } else if (field.type == NdeType::Float) {
                std::uint32_t bits = 0;
                std::memcpy(&bits, &field.real, 4);
                put32(payload, bits);
            } else if (field.type == NdeType::Int64) {
                put64(payload, static_cast<std::uint64_t>(field.number));
            } else {
                put32(payload, 0);
            }
            const std::uint32_t self = static_cast<std::uint32_t>(built.data.size());
            const std::uint32_t next =
                i + 1 < fields.size() ? self + 14 + static_cast<std::uint32_t>(payload.size()) : 0;
            const std::uint32_t previous =
                i > 0 ? static_cast<std::uint32_t>(built.data.size() - fields[i - 1].text.size() -
                                                   14 -
                                                   (fields[i - 1].type == NdeType::String ? 2 : 4))
                      : 0;
            built.data.push_back(static_cast<unsigned char>(field.column));
            built.data.push_back(static_cast<unsigned char>(field.type));
            put32(built.data, static_cast<std::uint32_t>(payload.size()));
            put32(built.data, next);
            put32(built.data, previous);
            built.data.insert(built.data.end(), payload.begin(), payload.end());
        }
    }
    built.index.insert(built.index.end(), {'N', 'D', 'E', 'I', 'N', 'D', 'E', 'X'});
    put32(built.index, static_cast<std::uint32_t>(offsets.size()));
    for (std::uint32_t offset : offsets) {
        put32(built.index, offset);
    }
    return built;
}

void writeFile(const fs::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

struct Fixture {
    fs::path dir;
    Fixture() {
        dir = fs::temp_directory_path() / "koalaamp-mi1-nde";
        std::error_code error;
        fs::remove_all(dir, error);
        fs::create_directories(dir, error);
    }
    ~Fixture() {
        std::error_code error;
        fs::remove_all(dir, error);
    }
    fs::path data() const { return dir / "main.dat"; }
    fs::path index() const { return dir / "main.idx"; }
};

} // namespace

TEST_CASE("NDE reader decodes the ml_local track columns") {
    Fixture fixture;
    const BuiltNde built = buildNde({
        {{0, NdeType::String, "C:\\Music\\one.flac"},
         {1, NdeType::String, "One"},
         {2, NdeType::String, "Artist"},
         {3, NdeType::String, "Album"},
         {4, NdeType::Integer, "", 2020},
         {5, NdeType::String, "Rock"},
         {6, NdeType::String, "Comment"},
         {7, NdeType::Integer, "", 4},
         {8, NdeType::Length, "", 215},
         {11, NdeType::DateTime, "", 1700000000},
         {12, NdeType::Integer, "", 4},
         {15, NdeType::Integer, "", 27},
         {19, NdeType::Integer, "", 1},
         {20, NdeType::String, "Album Artist"},
         {21, NdeType::Float, "", 0, -1.5F},
         {22, NdeType::Float, "", 0, 2.5F},
         {24, NdeType::String, "Composer"},
         {25, NdeType::Integer, "", 120},
         {41, NdeType::DateTime, "", 1800000000}},
        {{0, NdeType::String, "/music/two.ogg"}, {1, NdeType::String, "Two"}},
    });
    writeFile(fixture.data(), built.data);
    writeFile(fixture.index(), built.index);

    const NdeDatabase db = readNde(fixture.data(), fixture.index());
    REQUIRE(db.ok());
    REQUIRE(db.records.size() == 2);
    const std::vector<NdeTrack> tracks = ndeTracks(db);
    REQUIRE(tracks.size() == 2);
    CHECK(tracks[0].filename == "C:\\Music\\one.flac");
    CHECK(tracks[0].title == "One");
    CHECK(tracks[0].artist == "Artist");
    CHECK(tracks[0].album == "Album");
    CHECK(tracks[0].year == 2020);
    CHECK(tracks[0].genre == "Rock");
    CHECK(tracks[0].trackNo == 4);
    CHECK(tracks[0].durationMs == 215000);
    CHECK(tracks[0].lastPlayed == 1700000000);
    CHECK(tracks[0].rating == 4);
    CHECK(tracks[0].playCount == 27);
    CHECK(tracks[0].discNo == 1);
    CHECK(tracks[0].albumArtist == "Album Artist");
    CHECK(tracks[0].albumGain == -1.5);
    CHECK(tracks[0].trackGain == 2.5);
    CHECK(tracks[0].composer == "Composer");
    CHECK(tracks[0].bpm == 120);
    CHECK(tracks[0].dateAdded == 1800000000);
    CHECK(tracks[1].filename == "/music/two.ogg");
}

TEST_CASE("NDE reader rejects bad signatures and malformed indexes") {
    Fixture fixture;
    std::vector<unsigned char> junk(20, 0);
    writeFile(fixture.data(), junk);
    writeFile(fixture.index(), junk);
    CHECK_FALSE(readNde(fixture.data(), fixture.index()).ok());

    std::vector<unsigned char> data{'N', 'D', 'E', 'T', 'A', 'B', 'L', 'E'};
    std::vector<unsigned char> index{'N', 'D', 'E', 'I', 'N', 'D', 'E', 'X'};
    put32(index, 999999);
    writeFile(fixture.data(), data);
    writeFile(fixture.index(), index);
    CHECK_FALSE(readNde(fixture.data(), fixture.index()).ok());

    std::vector<unsigned char> shortIndex{'N', 'D', 'E', 'I', 'N', 'D', 'E', 'X'};
    put32(shortIndex, 1);
    writeFile(fixture.index(), shortIndex);
    CHECK_FALSE(readNde(fixture.data(), fixture.index()).ok());
}

TEST_CASE("NDE reader skips out-of-range, cyclic and truncated records") {
    Fixture fixture;
    std::vector<unsigned char> data{'N', 'D', 'E', 'T', 'A', 'B', 'L', 'E'};
    const std::uint32_t first = static_cast<std::uint32_t>(data.size());
    // One valid string field followed by a cyclic next pointer.
    data.push_back(0);
    data.push_back(static_cast<unsigned char>(NdeType::String));
    put32(data, 2);
    put32(data, first); // next == self -> cycle
    put32(data, 0);
    put16(data, 0);
    // A record whose payload runs past EOF.
    const std::uint32_t truncated = static_cast<std::uint32_t>(data.size());
    data.push_back(0);
    data.push_back(static_cast<unsigned char>(NdeType::String));
    put32(data, 100);
    put32(data, 0);
    put32(data, 0);
    data.push_back('x');

    std::vector<unsigned char> index{'N', 'D', 'E', 'I', 'N', 'D', 'E', 'X'};
    put32(index, 3);
    put32(index, 4); // out of range
    put32(index, first);
    put32(index, truncated);
    writeFile(fixture.data(), data);
    writeFile(fixture.index(), index);

    const NdeDatabase db = readNde(fixture.data(), fixture.index());
    REQUIRE(db.ok());
    CHECK(db.records.empty());
    CHECK(db.skippedRecords == 3);
}

TEST_CASE("NDE reader survives deterministic byte corruption") {
    Fixture fixture;
    const BuiltNde built =
        buildNde(std::vector<std::vector<FieldSpec>>{{{0, NdeType::String, "/music/a.flac"},
                                                      {1, NdeType::String, "A"},
                                                      {12, NdeType::Integer, "", 3}}});
    std::uint32_t state = 0x12345678;
    for (int round = 0; round < 100; ++round) {
        std::vector<unsigned char> data = built.data;
        std::vector<unsigned char> index = built.index;
        state = state * 1664525 + 1013904223;
        const std::size_t at = 8 + state % (data.size() - 8);
        data[at] ^= static_cast<unsigned char>(1U << (state % 8));
        writeFile(fixture.data(), data);
        writeFile(fixture.index(), index);
        const NdeDatabase db = readNde(fixture.data(), fixture.index());
        // Either the mutation is harmless or it is reported/skipped; never crash.
        CHECK(db.records.size() <= 1);
    }
}

TEST_CASE("Winamp paths.ini and winamp.ini are read case-insensitively") {
    const fs::path dir = fs::temp_directory_path() / "koalaamp-mi1-config";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir / "ini", error);
    fs::create_directories(dir / "playlists", error);
    {
        std::ofstream out(dir / "paths.ini");
        out << "[Winamp]\nIniDir=ini\nIniFile=player.ini\nM3uDir=playlists\n";
    }
    {
        std::ofstream out(dir / "ini" / "player.ini");
        out << "[Winamp]\n"
               "config_volume=200\n"
               "config_preamp=25\n"
               "config_shuffle=1\n"
               "config_repeat=0\n"
               "config_use_eq=1\n"
               "config_autoload_eq=1\n"
               "eq_type=1\n"
               "eq_limiter=0\n"
               "config_windowshade=1\n"
               "config_skin=\"C:\\Skins\\base.wsz\"\n"
               "config_titlefmt=\"[%artist% - ]%title%\"\n";
    }

    const WinampPaths paths = detectWinampPaths(dir);
    REQUIRE(paths.found());
    CHECK(paths.iniDir == dir / "ini");
    CHECK(paths.iniFile == dir / "ini" / "player.ini");
    CHECK(paths.playlistDir == dir / "playlists");
    CHECK(paths.skinsDir == dir / "Skins");

    const WinampState state = readWinampState(paths.iniFile);
    CHECK(state.volume == 200);
    CHECK(state.preamp == 25);
    CHECK(state.shuffle);
    CHECK_FALSE(state.repeat);
    CHECK(state.useEq);
    CHECK(state.autoloadEq);
    CHECK(state.eqType == 1);
    CHECK_FALSE(state.limiter);
    CHECK(state.windowShade);
    CHECK(state.skin == "C:\\Skins\\base.wsz");
    CHECK(state.titleFormat == "[%artist% - ]%title%");

    fs::remove_all(dir, error);
}

TEST_CASE("end-to-end NDE migration updates a matching library row idempotently") {
    Fixture fixture;
    const std::string path = (fixture.dir / "track.flac").string();
    const BuiltNde built =
        buildNde(std::vector<std::vector<FieldSpec>>{{{0, NdeType::String, path},
                                                      {1, NdeType::String, "Migrated"},
                                                      {2, NdeType::String, "Artist"},
                                                      {3, NdeType::String, "Album"},
                                                      {5, NdeType::String, "Rock"},
                                                      {6, NdeType::String, "Comment"},
                                                      {8, NdeType::Length, "", 215},
                                                      {11, NdeType::DateTime, "", 1700000000},
                                                      {12, NdeType::Integer, "", 4},
                                                      {15, NdeType::Integer, "", 27},
                                                      {21, NdeType::Float, "", 0, -1.5F},
                                                      {22, NdeType::Float, "", 0, 2.5F},
                                                      {25, NdeType::Integer, "", 120},
                                                      {41, NdeType::DateTime, "", 1690000000}}});
    writeFile(fixture.data(), built.data);
    writeFile(fixture.index(), built.index);
    {
        std::ofstream ini(fixture.dir / "winamp.ini");
        ini << "[Winamp]\nconfig_volume=200\nconfig_use_eq=1\neq_type=1\n";
    }
    {
        koalaamp::library::LibraryDb db(fixture.dir / "library.db");
        REQUIRE(db.open());
        koalaamp::library::ScannedTrack scanned;
        scanned.track.path = path;
        scanned.track.title = "Original";
        scanned.track.artist = "Artist";
        scanned.format = "flac";
        scanned.mtime = 1;
        scanned.size = 10;
        REQUIRE(db.upsert(scanned));
    }

    MigrationOptions options;
    options.winampDir = fixture.dir;
    options.databasePath = fixture.dir / "library.db";
    const MigrationReport first = runMigration(options);
    REQUIRE(first.ok());
    CHECK(first.records == 1);
    CHECK(first.matched == 1);
    CHECK(first.unmatched == 0);
    CHECK(first.state.volume == 200);
    CHECK(first.state.useEq);
    CHECK(first.state.eqType == 1);

    const auto read = [&]() {
        koalaamp::library::LibraryDb db(fixture.dir / "library.db");
        REQUIRE(db.open());
        const auto tracks = db.allTracks();
        REQUIRE(tracks.size() == 1);
        return tracks.front();
    };
    const auto after = read();
    CHECK(after.rating == 4);
    CHECK(after.playCount == 27);
    CHECK(after.lastPlayed == 1700000000);
    CHECK(after.dateAdded == 1690000000);
    CHECK(after.genre == "Rock");
    CHECK(after.comment == "Comment");
    CHECK(after.bpm == 120);
    CHECK(after.durationMs == 215000);

    const MigrationReport second = runMigration(options);
    REQUIRE(second.ok());
    CHECK(second.matched == 1);
    const auto again = read();
    CHECK(again.rating == after.rating);
    CHECK(again.playCount == after.playCount);
    CHECK(again.lastPlayed == after.lastPlayed);
    CHECK(again.dateAdded == after.dateAdded);
    CHECK(again.genre == after.genre);
    CHECK(again.comment == after.comment);
}
