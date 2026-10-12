// SPDX-License-Identifier: MIT
//
// SE-5 renderer tests (docs/tickets/SE-5-eq-window.md).
//
// Two layers:
//   * skin-independent geometry/formula assertions, which run everywhere;
//   * fixture assertions against the gitignored `base-2.91.wsz` (sprite
//     selection per state, integer scale, determinism) plus the committed
//     per-state golden hashes in tests/fixtures/se5/goldens.sha256.
//
// The rendering is additionally validated byte-for-byte against Webamp's own
// equalizer window by spikes/se5_eq/render-and-diff.sh (0 px on every golden
// state at 1x); that harness needs node + Chrome and so is not part of ctest.
//
// Golden regeneration: run the test binary with KOALAAMP_SE5_WRITE_GOLDENS=1
// (the fixture must be present) to rewrite the manifest, then review the diff.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <QImage>
#include <QPoint>
#include <QtGlobal>

#include <catch2/catch_test_macros.hpp>

#include "skins/SkinSet.hpp"
#include "ui/EqGoldenStates.hpp"
#include "ui/EqRenderer.hpp"
#include "ui/EqState.hpp"
#include "ui/GoldenStates.hpp"

namespace {

using koalaamp::skins::SkinSet;
using koalaamp::ui::EqWindowState;

const SkinSet* fixtureSkin() {
    static const std::optional<SkinSet> skin = []() -> std::optional<SkinSet> {
        const std::filesystem::path path = KOALAAMP_SE5_SKIN;
        if (!std::filesystem::exists(path)) {
            return std::nullopt;
        }
        return koalaamp::skins::loadSkinSet(path);
    }();
    return skin.has_value() ? &*skin : nullptr;
}

QImage render(const EqWindowState& state, int scale = 1) {
    return koalaamp::ui::renderEqWindow(fixtureSkin()->sheets, state, scale);
}

std::map<std::string, std::string> readManifest(const std::string& path) {
    std::map<std::string, std::string> entries;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream stream(line);
        std::string key;
        std::string digest;
        if (stream >> key >> digest) {
            entries[key] = digest;
        }
    }
    return entries;
}

/// Pixels where two frames differ.
std::vector<QPoint> diffPixels(const QImage& a, const QImage& b) {
    std::vector<QPoint> out;
    REQUIRE(a.size() == b.size());
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) {
                out.push_back(QPoint(x, y));
            }
        }
    }
    return out;
}

}  // namespace

TEST_CASE("SE-5 EQ slider formulas match Winamp/Webamp", "[se5][geometry]") {
    using namespace koalaamp::ui;
    // AU-1's inverted 0..63 field -> Webamp's 0..100 (100 = +12 dB at the top).
    CHECK(eqWebampValue(0) == 100);
    CHECK(eqWebampValue(63) == 0);
    CHECK(eqWebampValue(31) == 51);  // 0 dB, just above the middle
    CHECK(eqAuValueFromWebamp(100) == 0);
    CHECK(eqAuValueFromWebamp(0) == 63);

    // The groove strip is a 14x2 grid of 28 frames: round(value/100 * 27).
    CHECK(eqGrooveFrame(100) == 27);
    CHECK(eqGrooveFrame(0) == 0);
    CHECK(eqGrooveFrame(51) == 14);
    CHECK(eqGrooveSrcRect(0) == QRect(13, 164, 14, 63));
    CHECK(eqGrooveSrcRect(100) == QRect(208, 229, 14, 63));
    CHECK(eqGrooveSrcRect(50) == QRect(13, 229, 14, 63));

    // Thumb travel: floor((62 - 11) * (1 - value/100)).
    CHECK(eqThumbTop(100) == 0);
    CHECK(eqThumbTop(0) == 51);
    CHECK(eqThumbTop(51) == 24);

    // Band x positions.
    CHECK(eqBandX(0) == 21);   // preamp
    CHECK(eqBandX(1) == 78);   // 60 Hz
    CHECK(eqBandX(10) == 240); // 16 kHz

    // Mini shade sliders (Webamp's live DOM: volume 78 lands at x 134, balance
    // 0 at x 184 — matching the reference capture).
    CHECK(EqLayout::shadeVolume.x() + eqShadeThumbLeft(78, 0, 100, EqLayout::shadeVolume.width()) ==
          134);
    CHECK(EqLayout::shadeBalance.x() +
              eqShadeThumbLeft(0, -100, 100, EqLayout::shadeBalance.width()) ==
          184);
}

TEST_CASE("SE-5 EQ window renders 275x116 and shades to 275x14", "[se5][render][fixture]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    const EqWindowState* base = koalaamp::ui::eqGoldenState("eq_default");
    REQUIRE(base != nullptr);
    CHECK(render(*base).size() == QSize(275, 116));
    EqWindowState shaded = *base;
    shaded.shade = true;
    CHECK(render(shaded).size() == QSize(275, 14));
    // Deterministic.
    CHECK(koalaamp::ui::frameHash(render(*base)) == koalaamp::ui::frameHash(render(*base)));
}

TEST_CASE("SE-5 every control's state change is confined to its own rect",
          "[se5][render][fixture]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    using namespace koalaamp::ui;
    const EqWindowState base = *eqGoldenState("eq_default");
    const auto confined = [&](const EqWindowState& mutated, const std::vector<QRect>& allowed) {
        const auto changed = diffPixels(render(base), render(mutated));
        INFO("changed pixels: " << changed.size());
        REQUIRE_FALSE(changed.empty());
        for (const QPoint& point : changed) {
            const bool inside = std::any_of(allowed.begin(), allowed.end(),
                                            [&](const QRect& r) { return r.contains(point); });
            if (!inside) {
                FAIL("unexpected change at " << point.x() << "," << point.y());
            }
        }
    };

    {  // ON button
        EqWindowState state = base;
        state.enabled = true;
        confined(state, {EqLayout::onButton});
    }
    {  // AUTO button
        EqWindowState state = base;
        state.autoLoad = true;
        confined(state, {EqLayout::autoButton});
    }
    {  // PRESETS button held
        EqWindowState state = base;
        state.presetsPressed = true;
        confined(state, {EqLayout::presetsButton});
    }
    {  // the preamp slider changes the groove colour + thumb + the graph
        EqWindowState state = base;
        state.sliders[0] = kEqValueBoost;
        confined(state, {QRect(EqLayout::preamp.x(), EqLayout::bandY, EqLayout::bandWidth,
                               EqLayout::bandHeight),
                         EqLayout::graph});
    }
    {  // a band slider
        EqWindowState state = base;
        state.sliders[static_cast<std::size_t>(EqSlider::Hz1000)] = kEqValueCut;
        confined(state, {QRect(eqBandX(static_cast<int>(EqSlider::Hz1000)), EqLayout::bandY,
                               EqLayout::bandWidth, EqLayout::bandHeight),
                         EqLayout::graph});
    }
    {  // shade swaps the whole window
        EqWindowState state = base;
        state.shade = true;
        CHECK(render(state).size() == QSize(275, 14));
        const QImage& ex = *fixtureSkin()->sheets.sheet("EQ_EX");
        CHECK(render(state).pixel(0, 0) == ex.pixel(0, 15));  // idle shade art
    }
}

TEST_CASE("SE-5 integer 2x is the exact nearest-neighbour double of 1x",
          "[se5][render][fixture]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    for (const auto& [name, state] : koalaamp::ui::eqGoldenStateMatrix()) {
        const QImage one = render(state, 1);
        const QImage two = render(state, 2);
        REQUIRE(two.size() == QSize(one.width() * 2, one.height() * 2));
        bool exact = true;
        for (int y = 0; y < one.height() && exact; ++y) {
            for (int x = 0; x < one.width(); ++x) {
                const QRgb expected = one.pixel(x, y);
                if (two.pixel(x * 2, y * 2) != expected ||
                    two.pixel(x * 2 + 1, y * 2) != expected ||
                    two.pixel(x * 2, y * 2 + 1) != expected ||
                    two.pixel(x * 2 + 1, y * 2 + 1) != expected) {
                    INFO(name << " differs at " << x << "," << y);
                    exact = false;
                    break;
                }
            }
        }
        CHECK(exact);
    }
}

TEST_CASE("SE-5 per-state golden frames match the committed manifest",
          "[se5][golden][fixture]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    std::map<std::string, std::string> computed;
    for (const auto& [name, state] : koalaamp::ui::eqGoldenStateMatrix()) {
        for (const int scale : {1, 2}) {
            computed[name + "@" + std::to_string(scale) + "x"] =
                koalaamp::ui::frameHash(render(state, scale));
        }
    }

    if (qEnvironmentVariableIsSet("KOALAAMP_SE5_WRITE_GOLDENS")) {
        std::ofstream out(KOALAAMP_SE5_GOLDEN_MANIFEST, std::ios::trunc);
        out << "# SE-5 equalizer-window golden frames (name@Nx sha256).\n"
               "# Regenerate: KOALAAMP_SE5_WRITE_GOLDENS=1 <test binary>\n";
        for (const auto& [key, digest] : computed) {
            out << key << " " << digest << "\n";
        }
        WARN("wrote golden manifest: " << KOALAAMP_SE5_GOLDEN_MANIFEST);
        return;
    }

    const auto manifest = readManifest(KOALAAMP_SE5_GOLDEN_MANIFEST);
    INFO("manifest: " << KOALAAMP_SE5_GOLDEN_MANIFEST);
    REQUIRE_FALSE(manifest.empty());
    for (const auto& [key, digest] : computed) {
        const auto it = manifest.find(key);
        REQUIRE(it != manifest.end());
        INFO("state " << key);
        CHECK(digest == it->second);
    }
    for (const auto& [key, digest] : manifest) {
        (void)digest;
        CHECK(computed.count(key) == 1);  // no stale entries
    }
}
