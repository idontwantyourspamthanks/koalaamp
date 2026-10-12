// SPDX-License-Identifier: MIT
//
// SE-5 corpus render smoke (docs/tickets/SE-5-eq-window.md): every skin in the
// gitignored 30-skin corpus (scripts/fetch-corpus.sh) must load and render
// every equalizer-window state offscreen without crashing, at the right size,
// with actual art composited when the skin carries an `eqmain.bmp`. The corpus
// is the RLE8/1-bit/missing-member variety the base-2.91 fixture does not
// cover; this is a smoke test, not a pixel diff (no baseline exists for
// third-party artwork).
#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <QImage>

#include <catch2/catch_test_macros.hpp>

#include "skins/SkinSet.hpp"
#include "ui/EqGoldenStates.hpp"
#include "ui/EqRenderer.hpp"
#include "ui/EqState.hpp"

namespace {

std::vector<std::filesystem::path> corpusSkins() {
    std::vector<std::filesystem::path> paths;
    const std::filesystem::path dir = KOALAAMP_CORPUS_DIR;
    if (!std::filesystem::is_directory(dir)) {
        return paths;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const std::string extension = entry.path().extension().string();
        if (extension == ".wsz" || extension == ".zip") {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::size_t distinctColours(const QImage& image) {
    std::set<QRgb> colours;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            colours.insert(image.pixel(x, y));
        }
    }
    return colours.size();
}

bool isExactDouble(const QImage& one, const QImage& doubled) {
    if (doubled.size() != QSize(one.width() * 2, one.height() * 2)) {
        return false;
    }
    for (int y = 0; y < one.height(); ++y) {
        for (int x = 0; x < one.width(); ++x) {
            const QRgb expected = one.pixel(x, y);
            if (doubled.pixel(x * 2, y * 2) != expected ||
                doubled.pixel(x * 2 + 1, y * 2) != expected ||
                doubled.pixel(x * 2, y * 2 + 1) != expected ||
                doubled.pixel(x * 2 + 1, y * 2 + 1) != expected) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("SE-5 renders the whole skin corpus's equalizer without crashing",
          "[se5][corpus][fixture]") {
    const auto skins = corpusSkins();
    if (skins.empty()) {
        SKIP("corpus missing — run scripts/fetch-corpus.sh");
    }
    INFO("corpus skins: " << skins.size());
    CHECK(skins.size() >= 30);

    const auto& matrix = koalaamp::ui::eqGoldenStateMatrix();
    std::size_t rendered = 0;
    std::size_t withEqmain = 0;
    for (const auto& path : skins) {
        INFO("skin: " << path.filename().string());
        const auto skin = koalaamp::skins::loadSkinSet(path);
        REQUIRE(skin.has_value());
        REQUIRE(skin->isComplete());
        const bool hasEq = skin->hasSheet("EQMAIN");
        withEqmain += hasEq ? 1 : 0;
        for (const auto& [name, state] : matrix) {
            const QImage frame = koalaamp::ui::renderEqWindow(skin->sheets, state, 1);
            const QSize expected(275, state.shade ? 14 : 116);
            if (frame.size() != expected) {
                FAIL("state " << name << " rendered " << frame.width() << "x" << frame.height());
            }
            const QImage doubled = koalaamp::ui::renderEqWindow(skin->sheets, state, 2);
            if (!isExactDouble(frame, doubled)) {
                FAIL("state " << name << " is not an exact 2x nearest-neighbour double");
            }
            // A skin with real eqmain art must composite more than one colour in
            // the full window (windowshade art can legitimately be flat, and a
            // skin without eq_ex falls back to the eqmain title bar).
            if (hasEq && !state.shade && distinctColours(frame) < 2) {
                FAIL("state " << name << " painted a single colour despite eqmain.bmp");
            }
            ++rendered;
        }
    }
    INFO("state/skin renders: " << rendered << " (skins with eqmain: " << withEqmain << ")");
    CHECK(rendered == skins.size() * matrix.size());
}
