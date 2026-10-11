// SPDX-License-Identifier: MIT
//
// SE-1 corpus harness: loads every skin in the gitignored 30-skin quirk corpus
// (scripts/fetch-corpus.sh) and asserts that each one produces a complete
// `SkinSet` with valid geometry, no unhandled exception, and every retained
// region polygon having at least three points. A set of skins additionally pin
// their headline quirk (oversized/undersized art, 1-bit main, BI_RLE4 members,
// a 1900-polygon region, missing eq_ex, ...).
//
// The test SKIPs when the corpus has not been fetched — like the FE-3 fixture
// test — so a clean checkout stays green.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "skins/SkinSet.hpp"

using namespace koalaamp::skins;

#ifndef KOALAAMP_CORPUS_DIR
#define KOALAAMP_CORPUS_DIR ""
#endif

namespace {

/// Per-md5 quirk expectations. Only the fields that matter for that skin are set.
struct Expectation {
    int mainWidth = -1;
    int mainHeight = -1;
    int minRegionSections = -1;
    int minRegionPolygons = -1;
    int minCursors = -1;
    /// region.txt present but a template/hostile file: parses to nothing.
    bool regionEmpty = false;
    bool missingEqEx = false;
    bool missingEqMain = false;
    bool missingNumbers = false;
    bool missingNumsEx = false;
    bool undersizedWarning = false;
};

const std::map<std::string, Expectation>& expectations() {
    static const std::map<std::string, Expectation> kMap = {
        // Canonical base skin (10 BI_RLE8 sheets; region.txt is the tutorial
        // template, i.e. all comments).
        {"5e4f10275dcb1fb211d4a8b4f1bda236", {.mainWidth = 275, .mainHeight = 116, .regionEmpty = true}},
        // RLE8-heavy.
        {"3296b2c6f6c064416425d3382fd2e2b8", {.mainWidth = 275, .mainHeight = 116, .regionEmpty = true}},
        {"9dee1d5b8d326bdf2e35033f02e904f6", {.mainWidth = 277, .mainHeight = 118, .regionEmpty = true}},
        {"c151908ab31833320b8564d00926cd36", {.mainWidth = 275, .mainHeight = 116, .minRegionSections = 2}},
        {"89086b613253aead4bdca9ff97f16bf4", {.mainWidth = 275, .mainHeight = 116, .regionEmpty = true}},
        {"c346918d19112d063f23b2569eb543f2", {.mainWidth = 275, .mainHeight = 116, .minRegionSections = 3, .missingNumbers = true}},
        {"546f302a8766bad93bb75893411e60d9", {.mainWidth = 275, .mainHeight = 116}},
        {"0050f442e1438c491cb450f9fae047e4", {.mainWidth = 275, .mainHeight = 116}},
        // Depths.
        {"a764732f6cf9be4aaa8c3f66279abbde", {.mainWidth = 275, .mainHeight = 116}},  // 1-bit main
        {"412572dfad47302cf1190a9d57530520", {.mainWidth = 275, .mainHeight = 116}},  // 4-bit main + BI_RLE4
        {"21ac871ab461e1ff869829a8e4f9ee83", {.mainWidth = 275, .mainHeight = 116}},  // BI_RLE4
        {"33ff5ec701bb02c577042976b6cd847c", {.mainWidth = 275, .mainHeight = 116, .minRegionSections = 3}},
        {"3505c140b447fb42b942e06b707648c4", {.mainWidth = 275, .mainHeight = 116}},
        {"3b3b8b07fb7d268f6092d4321f0a9b9c", {.mainWidth = 275, .mainHeight = 116, .minRegionSections = 2, .missingNumbers = true}},
        {"69c6aa22b63ac6772bd2db1e678e1963", {.mainWidth = 276, .mainHeight = 116, .minRegionSections = 2, .minRegionPolygons = 200, .minCursors = 20}},
        // region.txt-heavy.
        {"7b39f961b0d80a96df69e447280e70a7", {.mainWidth = 275, .mainHeight = 116, .minRegionSections = 1, .minRegionPolygons = 1900}},
        {"be2de111c4710af306fea0813440f275", {.mainWidth = 275, .mainHeight = 116, .minRegionSections = 3, .minRegionPolygons = 700}},
        {"971c1afc05b56f5160903d0748b73239", {.mainWidth = 275, .mainHeight = 116, .minRegionSections = 4, .minRegionPolygons = 300}},
        {"07d31a296dc02a021fa6ddbcffeb5c5f", {.mainWidth = 275, .mainHeight = 116, .minRegionSections = 4}},
        {"2191f8d61c84edb4eb37ecf21d411d55", {.mainWidth = 275, .mainHeight = 116, .minRegionSections = 3, .minRegionPolygons = 3}},
        {"0eaba96cdab9ebac4d72f852aef6c69b", {.mainWidth = 275, .mainHeight = 116, .minRegionSections = 4, .missingEqEx = true}},
        // Missing optional files.
        {"1fc68f1376e8e1a7e15de659b3c89f36", {.mainWidth = 275, .mainHeight = 116, .minRegionSections = 2, .minRegionPolygons = 100, .missingEqEx = true, .missingNumbers = true}},
        {"211eb095dea514a244e0d57b824ac87e", {.mainWidth = 275, .mainHeight = 116, .missingEqEx = true, .missingNumsEx = true}},
        {"bba8100491e3785bb4e7726092693b4b", {.mainWidth = 275, .mainHeight = 116, .missingEqEx = true, .missingEqMain = true}},
        {"0e38ac23c5e02cba57b0825516be5279", {.mainWidth = 275, .mainHeight = 116, .missingNumbers = true}},
        // Geometry: oversized / undersized main.bmp.
        {"003dee624491506e1d736c6047ed1893", {.mainWidth = 307, .mainHeight = 116, .minCursors = 20}},
        {"2c4f1e5e7d2a1d385bb186dd057df8d6", {.mainWidth = 275, .mainHeight = 348}},
        {"1c3c947dbcd5935714dd33ed5e0c072e", {.mainWidth = 275, .mainHeight = 112, .undersizedWarning = true}},
        // Cursors + zero-size members.
        {"29bc9754ba7946311183a1acfc3347a2", {.mainWidth = 275, .mainHeight = 116, .minCursors = 20}},
        {"4269b10d8d27bd201f8608c59295680c", {.mainWidth = 275, .mainHeight = 116}},
    };
    return kMap;
}

std::vector<std::filesystem::path> corpusFiles() {
    std::vector<std::filesystem::path> files;
    const std::filesystem::path dir = KOALAAMP_CORPUS_DIR;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return files;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string ext = entry.path().extension().string();
        if (ext == ".wsz" || ext == ".zip") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool contains(const std::vector<std::string>& haystack, std::string_view needle) {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

}  // namespace

TEST_CASE("every corpus skin assembles into a complete SkinSet", "[se1][corpus]") {
    const auto files = corpusFiles();
    if (files.empty()) {
        SKIP("corpus missing — run scripts/fetch-corpus.sh");
    }

    std::size_t loaded = 0;
    std::vector<std::string> failures;
    for (const auto& path : files) {
        const std::string md5 = path.stem().string();
        auto set = loadSkinSet(path);
        if (!set) {
            failures.push_back(path.filename().string());
            continue;
        }
        ++loaded;

        INFO("skin " << path.filename().string());
        CHECK(set->isComplete());
        CHECK(set->info.mainWidth > 0);
        CHECK(set->info.mainHeight > 0);
        CHECK(set->hasSheet("MAIN"));
        CHECK(set->visColor.colors.size() == VisColor::kCount);
        CHECK_FALSE(set->pleditColors.font.empty());
        // Every polygon the leniency rules retain is a real polygon.
        for (const auto& [name, polygons] : set->region.sections) {
            for (const auto& polygon : polygons) {
                CHECK(polygon.points.size() >= 3);
            }
        }
        CHECK(set->warnings.size() < 64);

        const auto it = expectations().find(md5);
        if (it == expectations().end()) {
            continue;
        }
        const Expectation& e = it->second;
        if (e.mainWidth > 0) {
            CHECK(set->info.mainWidth == e.mainWidth);
        }
        if (e.mainHeight > 0) {
            CHECK(set->info.mainHeight == e.mainHeight);
        }
        if (e.minRegionSections >= 0) {
            CHECK(static_cast<int>(set->region.sections.size()) >= e.minRegionSections);
        }
        if (e.minRegionPolygons > 0) {
            CHECK(static_cast<int>(set->region.polygonCount()) >= e.minRegionPolygons);
        }
        if (e.minCursors > 0) {
            CHECK(static_cast<int>(set->cursors.size()) >= e.minCursors);
        }
        if (e.regionEmpty) {
            CHECK(set->region.empty());
        }
        if (e.missingEqEx) {
            CHECK(contains(set->missingOptional, "eq_ex.bmp"));
        }
        if (e.missingEqMain) {
            CHECK(contains(set->missingOptional, "eqmain.bmp"));
        }
        if (e.missingNumbers) {
            CHECK(contains(set->missingOptional, "numbers.bmp"));
        }
        if (e.missingNumsEx) {
            CHECK(contains(set->missingOptional, "nums_ex.bmp"));
        }
        if (e.undersizedWarning) {
            bool found = false;
            for (const auto& w : set->warnings) {
                found = found || w.find("is undersized") != std::string::npos;
            }
            CHECK(found);
        }
    }

    for (const auto& name : failures) {
        WARN("corpus skin failed to load: " << name);
    }
    INFO("corpus: " << files.size() << " files, " << loaded << " loaded, " << failures.size()
                    << " failed");
    CHECK(failures.empty());
    CHECK(loaded == files.size());
    CHECK(files.size() >= 30);  // the ticket's corpus size
}

TEST_CASE("corpus skin with a BI_RLE4 main still decodes its 4-bit sheets",
          "[se1][corpus]") {
    const std::filesystem::path path =
        std::filesystem::path(KOALAAMP_CORPUS_DIR) / "412572dfad47302cf1190a9d57530520.wsz";
    if (!std::filesystem::exists(path)) {
        SKIP("corpus missing — run scripts/fetch-corpus.sh");
    }
    auto set = loadSkinSet(path);
    REQUIRE(set.has_value());
    CHECK(set->isComplete());
    CHECK(set->sheetSize("main.bmp") == QSize(275, 116));
}
