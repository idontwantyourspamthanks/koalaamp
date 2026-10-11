// SPDX-License-Identifier: MIT
//
// SE-1 fuzz target: 100 deterministic mutated containers (truncated archives,
// zero-size members, bogus BMP headers, absurd geometry, hostile names) must not
// crash, throw or hang the loader; a graceful error is a pass. The run is
// bit-reproducible (fixed LCG seed) so CI failures are debuggable, and it is the
// target the ASAN/UBSAN parser build exercises.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "skins/SkinSet.hpp"
#include "skins_test_helpers.hpp"

using namespace koalaamp::skins;
using skins_test::buildZip;
using skins_test::makeBmp;
using skins_test::solidSheet;
using skins_test::TempDir;
using skins_test::ZipMember;

#ifndef KOALAAMP_FUZZ_SEED
#define KOALAAMP_FUZZ_SEED ""
#endif

namespace {

constexpr std::size_t kIterations = 100;

std::uint32_t nextRandom(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> seedBytes() {
    const std::filesystem::path real = KOALAAMP_FUZZ_SEED;
    if (!real.empty() && std::filesystem::exists(real)) {
        auto bytes = readFile(real);
        if (!bytes.empty()) {
            return bytes;
        }
    }
    // Fallback: a synthetic, structurally valid skin.
    return buildZip({
        {"main.bmp", solidSheet(275, 116, 5)},
        {"viscolor.txt", {'1', '2', ',', '3', '4', ',', '5', '6'}},
        {"region.txt", {'[', 'N', 'o', 'r', 'm', 'a', 'l', ']', '\n', 'N', 'u', 'm', 'P', 'o',
                        'i', 'n', 't', 's', '=', '4', '\n', 'P', 'o', 'i', 'n', 't', 'L', 'i',
                        's', 't', '=', '0', ',', '0', ' ', '9', ',', '0', ' ', '9', ',', '9',
                        ' ', '0', ',', '9'}},
    });
}

/// A BMP header that claims an enormous (or impossible) geometry.
std::vector<std::uint8_t> hostileBmp() {
    std::vector<std::uint8_t> b(64, 0);
    b[0] = 'B';
    b[1] = 'M';
    skins_test::put32(b, 14, 40);
    skins_test::put32(b, 18, 0x7FFFFFFFu);  // width
    skins_test::put32(b, 22, 0x7FFFFFFEu);  // height
    skins_test::put16(b, 28, 8);
    skins_test::put32(b, 10, 54);
    return b;
}

std::vector<ZipMember> pathologicalMembers(std::size_t variant) {
    switch (variant % 10) {
    case 0:  // zero-size members everywhere
        return {{"main.bmp", {}}, {"viscolor.txt", {}}, {"region.txt", {}}, {"text.bmp", {}}};
    case 1:  // bogus BMP header on main
        return {{"main.bmp", hostileBmp()}};
    case 2: {  // valid main, truncated RLE8 side sheet
        const std::vector<std::uint8_t> rle = {0x04, 0x01, 0x04};  // run byte with no value
        return {{"main.bmp", solidSheet(275, 116, 1)},
                {"numbers.bmp", makeBmp(99, 13, 8, 1, rle)}};
    }
    case 3:  // zero width/height BMP
        return {{"main.bmp", makeBmp(0, 0, 8, 0, {})}};
    case 4:  // hostile names
        return {{"main.bmp", solidSheet(275, 116, 2)},
                {"../../../main.bmp", solidSheet(275, 116, 3)},
                {"", std::vector<std::uint8_t>(4, 0)},
                {"dir\\sub\\MAIN.BMP", solidSheet(275, 116, 4)}};
    case 5:  // duplicate conflicting main entries
        return {{"main.bmp", solidSheet(275, 116, 1)}, {"MAIN.BMP", hostileBmp()}};
    case 6:  // garbage config files beside a valid main
        return {{"main.bmp", solidSheet(275, 116, 1)},
                {"region.txt", {'[', 'N', 'o', 'r', 'm', 'a', 'l', ']', '\n', 'N', 'u', 'm',
                                'P', 'o', 'i', 'n', 't', 's', '=', '9', '9', '9', '\n', 'P',
                                'o', 'i', 'n', 't', 'L', 'i', 's', 't', '=', '1', ',', '1'}},
                {"viscolor.txt", {0xFF, 0xFE, 0x00, 0x01}},
                {"pledit.txt", {'[', 'T', 'e', 'x', 't', ']', '\n', 'N', 'o', 'r', 'm', 'a',
                                'l', '=', '#', 'G', 'G', 'G'}}};
    case 7:  // deeply nested only
        return {{"a/b/c/main.bmp", solidSheet(275, 116, 1)}};
    case 8: {  // palette-claiming BMP with a truncated palette
        auto bmp = solidSheet(8, 8, 1);
        bmp.resize(60);
        return {{"main.bmp", bmp}};
    }
    default:  // 1-bit BMP whose bit rows are too short
        return {{"main.bmp", makeBmp(64, 64, 1, 0, std::vector<std::uint8_t>(8, 0xAA))}};
    }
}

}  // namespace

TEST_CASE("100 mutated containers fail gracefully, never crash", "[se1][fuzz]") {
    TempDir dir("fuzz");
    const std::vector<std::uint8_t> seed = seedBytes();
    REQUIRE_FALSE(seed.empty());

    std::uint32_t state = 0xC0FFEEu;
    std::size_t iterations = 0;
    std::size_t accepted = 0;
    std::size_t threw = 0;

    const auto exercise = [&](std::size_t index, const std::vector<std::uint8_t>& bytes) {
        ++iterations;
        CAPTURE(index, bytes.size());
        const auto path = dir.file("case-" + std::to_string(index) + ".wsz", bytes);
        std::optional<SkinSet> set;
        bool raised = false;
        try {
            set = loadSkinSet(path);
        } catch (...) {
            raised = true;
        }
        if (raised) {
            ++threw;
            FAIL("loader threw on mutation " << index);
            return;
        }
        if (set) {
            ++accepted;
            // Whatever survives must be internally consistent.
            CHECK(set->isComplete());
            CHECK(set->info.mainWidth > 0);
            CHECK(set->info.mainHeight > 0);
            for (const auto& [name, polygons] : set->region.sections) {
                for (const auto& polygon : polygons) {
                    CHECK(polygon.points.size() >= 3);
                }
            }
        }
    };

    // 60 byte-level mutations of the seed archive.
    for (std::size_t i = 0; i < 60; ++i) {
        std::vector<std::uint8_t> bytes = seed;
        const std::uint32_t kind = nextRandom(state) % 3;
        if (kind == 0) {
            const std::size_t keep = 1 + (nextRandom(state) % bytes.size());
            bytes.resize(keep);
        } else if (kind == 1) {
            const std::uint32_t flips = 1 + (nextRandom(state) % 16);
            for (std::uint32_t f = 0; f < flips && !bytes.empty(); ++f) {
                const std::size_t at = nextRandom(state) % bytes.size();
                bytes[at] = static_cast<std::uint8_t>(bytes[at] ^ (nextRandom(state) & 0xFF));
            }
        } else {
            const std::size_t at = nextRandom(state) % bytes.size();
            const std::size_t len = std::min<std::size_t>(16, bytes.size() - at);
            std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                      bytes.begin() + static_cast<std::ptrdiff_t>(at + len), 0);
        }
        exercise(i, bytes);
    }

    // 20 synthesized pathological containers (truncated to a random length).
    for (std::size_t i = 0; i < 20; ++i) {
        std::vector<std::uint8_t> bytes = buildZip(pathologicalMembers(i));
        if (nextRandom(state) % 2 == 0 && !bytes.empty()) {
            bytes.resize(1 + (nextRandom(state) % bytes.size()));
        }
        exercise(60 + i, bytes);
    }

    // 20 more archives with extreme member counts / sizes.
    for (std::size_t i = 0; i < 20; ++i) {
        std::vector<ZipMember> members;
        const std::size_t count = (i % 2 == 0) ? 9000 : 50;  // 9000 exercises the entry cap
        members.reserve(count);
        for (std::size_t m = 0; m < count; ++m) {
            members.push_back({"member" + std::to_string(m) + ".bmp",
                               solidSheet(2, 2, static_cast<std::uint8_t>(m & 0xFF))});
        }
        if (i % 3 == 0) {
            members.push_back({"main.bmp", solidSheet(275, 116, 1)});
        }
        exercise(80 + i, buildZip(members));
    }

    INFO("fuzz: " << iterations << " containers, " << accepted << " assembled, " << threw
                  << " threw");
    CHECK(iterations == kIterations);
    CHECK(threw == 0);
    // Most hostile inputs are rejected; the loader must still accept the
    // structurally-sane mutants (so the target is not vacuous).
    CHECK(accepted > 0);
}
