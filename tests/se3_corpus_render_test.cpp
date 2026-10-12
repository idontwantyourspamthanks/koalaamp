// SPDX-License-Identifier: MIT
//
// SE-3 corpus render smoke (docs/tickets/SE-3-playlist-window.md): every skin
// in the gitignored 30-skin corpus (scripts/fetch-corpus.sh) must render every
// playlist-window state offscreen without crashing, at the right size, with the
// 2x frame the exact nearest-neighbour double of 1x. Smoke, not pixel-diff:
// there is no baseline for third-party artwork.
#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <QImage>

#include <catch2/catch_test_macros.hpp>

#include "skins/SkinSet.hpp"
#include "ui/PleditGoldens.hpp"
#include "ui/PleditRenderer.hpp"
#include "ui/PleditState.hpp"

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

TEST_CASE("SE-3 renders the whole skin corpus without crashing",
          "[se3][corpus][fixture]") {
    const auto skins = corpusSkins();
    if (skins.empty()) {
        SKIP("corpus missing — run scripts/fetch-corpus.sh");
    }
    INFO("corpus skins: " << skins.size());
    CHECK(skins.size() >= 30);

    const auto& matrix = koalaamp::ui::pleditGoldenMatrix();
    std::size_t rendered = 0;
    for (const auto& path : skins) {
        INFO("skin: " << path.filename().string());
        const auto skin = koalaamp::skins::loadSkinSet(path);
        REQUIRE(skin.has_value());
        REQUIRE(skin->isComplete());
        for (const auto& [name, state] : matrix) {
            const QImage frame = koalaamp::ui::renderPleditWindow(
                skin->sheets, koalaamp::ui::pleditGoldenColors(), state, 1);
            const QSize expected(koalaamp::ui::pleditWidth(state),
                                 state.shade ? 14 : koalaamp::ui::pleditHeight(state));
            if (frame.size() != expected) {
                FAIL("state " << name << " rendered " << frame.width() << "x" << frame.height());
            }
            const QImage doubled = koalaamp::ui::renderPleditWindow(
                skin->sheets, koalaamp::ui::pleditGoldenColors(), state, 2);
            if (!isExactDouble(frame, doubled)) {
                FAIL("state " << name << " is not an exact 2x nearest-neighbour double");
            }
            ++rendered;
        }
        const koalaamp::ui::PleditState* base = koalaamp::ui::pleditGoldenState("default");
        REQUIRE(base != nullptr);
        const QImage frame = koalaamp::ui::renderPleditWindow(
            skin->sheets, koalaamp::ui::pleditGoldenColors(), *base, 1);
        CHECK(distinctColours(frame) >= 2);
    }
    INFO("state/skin renders: " << rendered);
    CHECK(rendered == skins.size() * matrix.size());
}
