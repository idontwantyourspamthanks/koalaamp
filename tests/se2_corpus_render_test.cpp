// SPDX-License-Identifier: MIT
//
// SE-2 corpus render smoke (docs/tickets/SE-2-main-window.md): every skin in
// the gitignored 30-skin corpus (scripts/fetch-corpus.sh) must load and render
// every main-window state offscreen without crashing, at the right size, with
// actual art composited. The corpus is the RLE8/1-bit/missing-member variety
// that the base-2.91 fixture does not cover; this is a smoke test, not a pixel
// diff (no baseline exists for third-party artwork).
#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <QImage>

#include <catch2/catch_test_macros.hpp>

#include "skins/SkinSet.hpp"
#include "ui/GoldenStates.hpp"
#include "ui/PlayerRenderer.hpp"
#include "ui/PlayerState.hpp"

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

/// Distinct colours in a frame — a cheap "something was actually painted" probe.
/// Corpus skins include 1-bit black/white art, so this only proves the frame is
/// not a single fill colour; the stronger integrity check is that the 2x frame
/// is the exact nearest-neighbour double of the 1x one.
std::size_t distinctColours(const QImage& image) {
    std::set<QRgb> colours;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            colours.insert(image.pixel(x, y));
        }
    }
    return colours.size();
}

/// True when `doubled` is `one` with every pixel replicated 2x2.
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

TEST_CASE("SE-2 renders the whole skin corpus without crashing",
          "[se2][corpus][fixture]") {
    const auto skins = corpusSkins();
    if (skins.empty()) {
        SKIP("corpus missing — run scripts/fetch-corpus.sh");
    }
    INFO("corpus skins: " << skins.size());
    CHECK(skins.size() >= 30);

    const auto& matrix = koalaamp::ui::goldenStateMatrix();
    std::size_t rendered = 0;
    for (const auto& path : skins) {
        INFO("skin: " << path.filename().string());
        const auto skin = koalaamp::skins::loadSkinSet(path);
        REQUIRE(skin.has_value());
        REQUIRE(skin->isComplete());
        for (const auto& [name, state] : matrix) {
            const QImage frame = koalaamp::ui::renderMainWindow(skin->sheets, skin->visColor,
                                                               state, 1);
            const QSize expected(275, state.shade ? 14 : 116);
            if (frame.size() != expected) {
                FAIL("state " << name << " rendered " << frame.width() << "x" << frame.height());
            }
            const QImage doubled = koalaamp::ui::renderMainWindow(skin->sheets, skin->visColor,
                                                                 state, 2);
            if (!isExactDouble(frame, doubled)) {
                FAIL("state " << name << " is not an exact 2x nearest-neighbour double");
            }
            ++rendered;
        }
        // The default state must carry real art, not a single fill colour.
        const koalaamp::ui::PlayerState* base = koalaamp::ui::goldenState("default");
        REQUIRE(base != nullptr);
        const QImage frame =
            koalaamp::ui::renderMainWindow(skin->sheets, skin->visColor, *base, 1);
        CHECK(distinctColours(frame) >= 2);
    }
    INFO("state/skin renders: " << rendered);
    CHECK(rendered == skins.size() * matrix.size());
}
