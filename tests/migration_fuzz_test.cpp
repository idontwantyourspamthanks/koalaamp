// SPDX-License-Identifier: MIT
// MI-1 fuzz pass: mutated NDE data/index and bookmark files must never crash,
// over-read, or grow without bound. This is the ASAN/UBSAN target named by the
// ticket; it is also a normal, deterministic unit test.

#include "migration/Bookmarks.hpp"
#include "migration/NDE.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace koalaamp::migration;

struct Field {
    int column;
    NdeType type;
    std::string text;
    std::int64_t number{0};
};

void put16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xff));
    out.push_back(static_cast<unsigned char>(value >> 8));
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

struct Built {
    std::vector<unsigned char> data;
    std::vector<unsigned char> index;
};

Built build(const std::vector<std::vector<Field>>& records) {
    Built built;
    built.data.insert(built.data.end(), {'N', 'D', 'E', 'T', 'A', 'B', 'L', 'E'});
    std::vector<std::uint32_t> offsets;
    for (const auto& fields : records) {
        offsets.push_back(static_cast<std::uint32_t>(built.data.size()));
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const Field& field = fields[i];
            std::vector<unsigned char> payload;
            if (field.type == NdeType::Column) {
                payload.push_back(static_cast<unsigned char>(NdeType::String));
                payload.push_back(0);
                payload.push_back(static_cast<unsigned char>(field.text.size()));
                payload.insert(payload.end(), field.text.begin(), field.text.end());
            } else if (field.type == NdeType::String || field.type == NdeType::Filename) {
                put16(payload, static_cast<std::uint16_t>(field.text.size()));
                payload.insert(payload.end(), field.text.begin(), field.text.end());
            } else if (field.type == NdeType::Integer || field.type == NdeType::DateTime ||
                       field.type == NdeType::Length) {
                put32(payload, static_cast<std::uint32_t>(field.number));
            } else if (field.type == NdeType::Boolean) {
                payload.push_back(field.number != 0 ? 1 : 0);
            } else {
                put64(payload, static_cast<std::uint64_t>(field.number));
            }
            const std::uint32_t self = static_cast<std::uint32_t>(built.data.size());
            const std::uint32_t next =
                i + 1 < fields.size() ? self + 14 + static_cast<std::uint32_t>(payload.size()) : 0;
            built.data.push_back(static_cast<unsigned char>(field.column));
            built.data.push_back(static_cast<unsigned char>(field.type));
            put32(built.data, static_cast<std::uint32_t>(payload.size()));
            put32(built.data, next);
            put32(built.data, 0);
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

} // namespace

TEST_CASE("fuzz: mutated NDE and bookmark inputs stay bounded") {
    const fs::path dir = fs::temp_directory_path() / "koalaamp-mi1-fuzz";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir, error);

    const Built base = build({
        {{0, NdeType::Column, "filename"},
         {1, NdeType::Column, "title"},
         {2, NdeType::Column, "lastplay"}},
        {{0, NdeType::String, "/music/a.flac"},
         {1, NdeType::String, "A"},
         {2, NdeType::DateTime, "", 1700000000}},
    });
    const std::string bookmarkBase = "Header\nA|/music/a.flac\nB|/music/b.flac\nC|/music/c.flac\n";

    std::mt19937 rng(0x5eed1234);
    for (int round = 0; round < 300; ++round) {
        std::vector<unsigned char> data = base.data;
        std::vector<unsigned char> index = base.index;
        for (int mutation = 0; mutation < 1 + round % 3; ++mutation) {
            const std::size_t at = 8 + rng() % (data.size() - 8);
            data[at] ^= static_cast<unsigned char>(1U << (rng() % 8));
        }
        if (round % 5 == 0 && index.size() > 12) {
            const std::size_t at = 12 + rng() % (index.size() - 12);
            index[at] ^= static_cast<unsigned char>(1U << (rng() % 8));
        }
        writeFile(dir / "main.dat", data);
        writeFile(dir / "main.idx", index);

        const NdeDatabase db = readNde(dir / "main.dat", dir / "main.idx");
        CHECK(db.records.size() <= 8);
        CHECK(db.columns.size() <= 8);
        if (db.ok()) {
            CHECK(ndeTracks(db).size() <= db.records.size());
            CHECK(ndeHistory(db).size() <= db.records.size());
        }

        std::string bookmarks = bookmarkBase;
        for (int mutation = 0; mutation < 1 + round % 4; ++mutation) {
            bookmarks[rng() % bookmarks.size()] = static_cast<char>(rng() & 0xff);
        }
        const BookmarkFile parsed = parseBookmarks(bookmarks);
        CHECK(parsed.bookmarks.size() <= 8);
    }
    fs::remove_all(dir, error);
}
