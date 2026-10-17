// SPDX-License-Identifier: MIT
//
// SE-3 renderer tests (docs/tickets/SE-3-playlist-window.md).
//
// Skin-independent geometry/row-state assertions run everywhere; the fixture
// assertions (sprite composition, integer scale, the committed golden manifest)
// use the gitignored `base-2.91.wsz` (spikes/fe3_wsz/fetch-skin.sh) and skip
// when it is absent.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <QColor>
#include <QGuiApplication>
#include <QImage>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "skins/SkinSet.hpp"
#include "ui/GoldenStates.hpp"
#include "ui/PleditGoldens.hpp"
#include "ui/PleditRenderer.hpp"
#include "ui/PleditState.hpp"

namespace {

using koalaamp::skins::PleditColors;
using koalaamp::skins::SkinSet;
using namespace koalaamp::ui;

const SkinSet* fixtureSkin() {
    static const std::optional<SkinSet> skin = []() -> std::optional<SkinSet> {
        const std::filesystem::path path = KOALAAMP_SE3_SKIN;
        if (!std::filesystem::exists(path)) {
            return std::nullopt;
        }
        return koalaamp::skins::loadSkinSet(path);
    }();
    return skin.has_value() ? &*skin : nullptr;
}

QImage render(const PleditState& state, int scale = 1) {
    return renderPleditWindow(fixtureSkin()->sheets, pleditGoldenColors(), state, scale);
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

TEST_CASE("SE-3 playlist geometry tiles in 25x29 segments", "[se3][geometry]") {
    PleditState state;
    CHECK(pleditWidth(state) == 275);
    CHECK(pleditHeight(state) == 116);
    CHECK(pleditMiddleHeight(0) == 58);
    CHECK(pleditVisibleTracks(0) == 4);

    state.widthSegments = 2;
    state.heightSegments = 1;
    CHECK(pleditWidth(state) == 325);
    CHECK(pleditHeight(state) == 145);
    CHECK(pleditVisibleTracks(1) == 6);

    // Bands stack: 20 px top + middle + 38 px bottom == the window height.
    const QRect middle = pleditMiddle(state);
    CHECK(middle.top() == kPleditTopHeight);
    CHECK(middle.height() == pleditMiddleHeight(1));
    const QRect bottom = pleditBottom(state);
    CHECK(bottom.top() == kPleditTopHeight + pleditMiddleHeight(1));
    CHECK(bottom.bottom() + 1 == pleditHeight(state));

    CHECK(pleditMiddleLeft(state).width() == 12);
    CHECK(pleditMiddleRight(state).width() == 20);
    CHECK(pleditListArea(state).left() == 12);
    CHECK(pleditListArea(state).right() == pleditWidth(state) - 21);

    // Rows are 13 px apart starting after the 3 px pad.
    CHECK(pleditTrackRow(state, 0).top() == middle.top() + kPleditListPad);
    CHECK(pleditTrackRow(state, 1).top() - pleditTrackRow(state, 0).top() ==
          kPleditTrackHeight);

    // Title bar and its buttons in the top-right corner.
    CHECK(pleditTopLeftCorner(state).width() == 25);
    CHECK(pleditTopRightCorner(state).left() == pleditWidth(state) - 25);
    CHECK(pleditCloseButton(state).right() == pleditWidth(state) - 3);
    CHECK(pleditShadeButton(state).right() == pleditCloseButton(state).left() - 2);

    // The bottom-right band holds the resize grip at the very corner.
    const QRect grip = pleditResizeGrip(state);
    CHECK(grip.right() == pleditWidth(state) - 1);
    CHECK(grip.bottom() == pleditHeight(state) - 1);
}

TEST_CASE("SE-3 bottom-band pieces all live in the bottom band", "[se3][geometry]") {
    // Regression guard: every piece painted into the band must sit inside it.
    // The 125 px left corner carries the baked ADD/REM/SEL/MISC buttons, so
    // painting it anywhere else peels the button strip off the band and plaster
    // it over the title bar — `pleditBottomLeft` returned QRect(0, 0, 125, 38)
    // (a band y of zero), which is exactly how the playlist window came to look
    // like a pile of loose art.
    for (const int widthSegments : {0, 1, 2, 5}) {
        for (const int heightSegments : {0, 1, 3}) {
            PleditState state;
            state.widthSegments = widthSegments;
            state.heightSegments = heightSegments;
            const QRect band = pleditBottom(state);
            CAPTURE(widthSegments, heightSegments);

            const std::vector<QRect> pieces = {
                pleditBottomLeft(state),     pleditBottomRight(state),
                pleditVisualizer(state),     pleditRunningTime(state),
                pleditMiniTime(state),       pleditResizeGrip(state),
                pleditMenuButton(state, 0),  pleditMenuButton(state, 3),
                pleditActionButton(state, 0), pleditActionButton(state, 5),
            };
            for (const QRect& piece : pieces) {
                CHECK(piece.top() >= band.top());
                CHECK(piece.bottom() <= band.bottom());
            }

            // The button corner is the band's left 125 px, flush with its top.
            CHECK(pleditBottomLeft(state).left() == 0);
            CHECK(pleditBottomLeft(state).top() == band.top());
            CHECK(pleditBottomLeft(state).width() == 125);
        }
    }
}

TEST_CASE("SE-3 row visuals encode selected / current / queued / hover",
          "[se3][geometry]") {
    const PleditColors colors = pleditGoldenColors();
    PleditState state;
    state.entries = {PleditEntry{"a", "", 0}, PleditEntry{"b", "", 0},
                     PleditEntry{"c", "", 0}, PleditEntry{"d", "", 0}};
    state.currentIndex = 1;
    enqueueIndex(state, 2);
    selectSingle(state, 3);
    state.hoverIndex = 0;

    const PleditRowVisual plain = pleditRowVisual(colors, state, 0);
    CHECK(plain.background == QColor(colors.normalBg.r, colors.normalBg.g, colors.normalBg.b));
    CHECK(plain.text == QColor(colors.current.r, colors.current.g, colors.current.b));  // hover
    CHECK_FALSE(plain.queued);

    const PleditRowVisual current = pleditRowVisual(colors, state, 1);
    CHECK(current.text == QColor(colors.current.r, colors.current.g, colors.current.b));

    const PleditRowVisual queued = pleditRowVisual(colors, state, 2);
    CHECK(queued.queued);
    CHECK(queued.text == QColor(colors.normal.r, colors.normal.g, colors.normal.b));

    const PleditRowVisual selected = pleditRowVisual(colors, state, 3);
    CHECK(selected.background ==
          QColor(colors.selectedBg.r, colors.selectedBg.g, colors.selectedBg.b));

    // The label carries the queue marker and the row number.
    CHECK(pleditRowLabel(state, 2) == "3. c ^1");
    CHECK(pleditRowLabel(state, 0) == "1. a");
}

TEST_CASE("SE-3 renders the right size in every layout", "[se3][render][fixture]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    const PleditState* base = pleditGoldenState("default");
    REQUIRE(base != nullptr);
    CHECK(render(*base).size() == QSize(275, 116));

    PleditState wide = *base;
    wide.widthSegments = 2;
    CHECK(render(wide).size() == QSize(325, 116));

    PleditState tall = *base;
    tall.heightSegments = 2;
    CHECK(render(tall).size() == QSize(275, 174));

    PleditState shaded = *base;
    shaded.shade = true;
    CHECK(render(shaded).size() == QSize(275, 14));

    // Deterministic: two renders of the same state are byte-identical.
    CHECK(frameHash(render(*base)) == frameHash(render(*base)));
}

TEST_CASE("SE-3 paints every column of the title band", "[se3][render][fixture]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    // Regression guard: the band between the corners is covered by the tiles, the
    // opaque 100 px title strip and the corner buttons. Filling only the regions
    // *beside* the strip left the 12/13 px spacer columns unpainted, so the canvas
    // fill showed through as two black bars in the title band.
    const PleditColors colors = pleditGoldenColors();
    const QColor background(colors.normalBg.r, colors.normalBg.g, colors.normalBg.b);
    for (const auto& [name, state] : pleditGoldenMatrix()) {
        if (state.shade) {
            continue;  // the shaded band is a different, fully tiled layout
        }
        INFO("state " << name);
        const QImage frame = render(state);
        const QRect left = pleditTopLeftCorner(state);
        const QRect right = pleditTopRightCorner(state);
        for (int x = left.right() + 1; x < right.left(); ++x) {
            bool painted = false;
            for (int y = 0; y < kPleditTopHeight; ++y) {
                painted = painted || frame.pixelColor(x, y) != background;
            }
            INFO("unpainted column x=" << x);
            CHECK(painted);
        }
    }
}

TEST_CASE("SE-3 integer 2x is the exact nearest-neighbour double of 1x",
          "[se3][render][fixture]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    for (const auto& [name, state] : pleditGoldenMatrix()) {
        INFO("state " << name);
        CHECK(isExactDouble(render(state, 1), render(state, 2)));
    }
}

TEST_CASE("SE-3 per-state golden frames match the committed manifest",
          "[se3][golden][fixture]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    const auto manifest = readManifest(KOALAAMP_SE3_GOLDEN_MANIFEST);
    INFO("manifest: " << KOALAAMP_SE3_GOLDEN_MANIFEST);
    REQUIRE_FALSE(manifest.empty());
    for (const auto& [name, state] : pleditGoldenMatrix()) {
        for (const int scale : {1, 2}) {
            const std::string key = name + "@" + std::to_string(scale) + "x";
            const auto it = manifest.find(key);
            REQUIRE(it != manifest.end());
            INFO("state " << key);
            CHECK(frameHash(render(state, scale)) == it->second);
        }
    }
    std::set<std::string> expected;
    for (const auto& [name, state] : pleditGoldenMatrix()) {
        (void)state;
        expected.insert(name + "@1x");
        expected.insert(name + "@2x");
    }
    for (const auto& [key, digest] : manifest) {
        (void)digest;
        CHECK(expected.count(key) == 1);
    }
}

TEST_CASE("SE-3 track text falls back to the 5x6 font when Font= is absent",
          "[se3][render][fixture]") {
    CHECK_FALSE(pleditFontAvailable(""));
    CHECK_FALSE(pleditFontAvailable("KoalaAmp-No-Such-Font-1234"));
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    const PleditState* base = pleditGoldenState("default");
    REQUIRE(base != nullptr);
    PleditColors a = pleditGoldenColors();
    PleditColors b = pleditGoldenColors();
    b.font = "KoalaAmp-No-Such-Font-1234";  // also unavailable: same fallback
    CHECK(frameHash(renderPleditWindow(fixtureSkin()->sheets, a, *base, 1)) ==
          frameHash(renderPleditWindow(fixtureSkin()->sheets, b, *base, 1)));
}

TEST_CASE("SE-3 a 10k-row playlist scrolls within one frame budget",
          "[se3][render][perf]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    PleditState state;
    state.entries.reserve(10'000);
    for (int i = 0; i < 10'000; ++i) {
        state.entries.push_back(PleditEntry{"Artist - Track " + std::to_string(i + 1),
                                            "/music/track-" + std::to_string(i) + ".mp3",
                                            180'000 + i});
    }
    state.heightSegments = 4;  // ~12 visible rows

    const int frames = 240;
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < frames; ++frame) {
        state.scrollOffset = frame % 5000;
        const QImage image =
            renderPleditWindow(fixtureSkin()->sheets, pleditGoldenColors(), state, 1);
        REQUIRE(image.size() == QSize(275, 116 + 4 * 29));
    }
    const double perFrameMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count() /
        frames;
    std::printf("[se3][perf] 10k-row frame: %.3f ms/frame (%.2f%% of a 60fps budget)\n",
                perFrameMs, perFrameMs / 16.667 * 100.0);
    // The renderer is a pure function over the *visible* rows only; a frame is
    // far under the 16.7 ms 60fps budget.
    CHECK(perFrameMs < 5.0);
}

int main(int argc, char** argv) {
    // QGuiApplication is required by QFontDatabase (pleditFontAvailable).
    QGuiApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
