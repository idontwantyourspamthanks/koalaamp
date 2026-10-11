// SPDX-License-Identifier: MIT
//
// SE-2 renderer tests (docs/tickets/SE-2-main-window.md).
//
// Two layers:
//   * skin-independent geometry/formula/state-machine assertions, which run
//     everywhere;
//   * fixture assertions against the gitignored `base-2.91.wsz`
//     (spikes/fe3_wsz/fetch-skin.sh) — sprite selection per state, integer
//     scale, determinism, the visualiser rules, and the committed per-state
//     golden hashes in tests/fixtures/se2/goldens.sha256.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <QColor>
#include <QImage>

#include <catch2/catch_test_macros.hpp>

#include "skins/SkinSet.hpp"
#include "ui/GoldenStates.hpp"
#include "ui/PlayerRenderer.hpp"
#include "ui/PlayerState.hpp"
#include "ui/TitleText.hpp"
#include "ui/VisData.hpp"

namespace {

using koalaamp::skins::SkinSet;
using koalaamp::ui::PlayerState;
using koalaamp::ui::PlayerStatus;
using koalaamp::ui::VisMode;

const SkinSet* fixtureSkin() {
    static const std::optional<SkinSet> skin = []() -> std::optional<SkinSet> {
        const std::filesystem::path path = KOALAAMP_SE2_SKIN;
        if (!std::filesystem::exists(path)) {
            return std::nullopt;
        }
        return koalaamp::skins::loadSkinSet(path);
    }();
    return skin.has_value() ? &*skin : nullptr;
}

QImage render(const PlayerState& state, int scale = 1) {
    return koalaamp::ui::renderMainWindow(fixtureSkin()->sheets, fixtureSkin()->visColor, state,
                                          scale);
}

/// Pixels where two frames differ, as (x, y) pairs.
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

/// Asserts that rendering `mutated` differs from `base` and that every
/// differing pixel lies inside one of `allowed`.
void checkChangeConfined(const PlayerState& base, const PlayerState& mutated,
                         const std::vector<QRect>& allowed, int scale = 1) {
    const auto changed = diffPixels(render(base, scale), render(mutated, scale));
    INFO("changed pixels: " << changed.size());
    REQUIRE_FALSE(changed.empty());
    for (const QPoint& point : changed) {
        const bool inside = std::any_of(allowed.begin(), allowed.end(), [&](const QRect& rect) {
            return rect.contains(point);
        });
        if (!inside) {
            FAIL("unexpected change at " << point.x() << "," << point.y());
        }
    }
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

}  // namespace

TEST_CASE("SE-2 slider and frame formulas match Winamp/Webamp", "[se2][geometry]") {
    using namespace koalaamp::ui;
    // Volume: 28 frames at 15 px, sprite = round(pct*28), offset = (sprite-1)*15.
    CHECK(volumeFrameOffset(0) == 0);      // clamped to the first frame
    CHECK(volumeFrameOffset(1) == 0);      // sprite 0 clamps to 1
    CHECK(volumeFrameOffset(100) == 405);  // sprite 28
    CHECK(volumeFrameOffset(78) == 315);   // Webamp's default volume
    CHECK(volumeFrameOffset(50) == 195);   // sprite 14
    // Balance: 27 frames, sprite = floor(|bal|*27/100), offset = sprite*15.
    CHECK(balanceFrameOffset(0) == 0);
    CHECK(balanceFrameOffset(100) == 405);
    CHECK(balanceFrameOffset(-100) == 405);
    CHECK(balanceFrameOffset(-50) == 195);
    // Thumb travel: round(fraction * (track - thumb)).
    CHECK(thumbLeft(0, 0, 100, 65, 14) == 0);
    CHECK(thumbLeft(100, 0, 100, 65, 14) == 51);
    CHECK(thumbLeft(78, 0, 100, 65, 14) == 40);  // Webamp's default volume thumb
    CHECK(thumbLeft(-100, -100, 100, 38, 14) == 0);
    CHECK(thumbLeft(0, -100, 100, 38, 14) == 12);
    CHECK(thumbLeft(100, -100, 100, 38, 14) == 24);
    CHECK(thumbLeft(50, 0, 100, 248, 29) == 110);
    CHECK(thumbLeft(100, 0, 100, 248, 29) == 219);
}

TEST_CASE("SE-2 kbps/khz fields format like Webamp's PE/QE", "[se2][geometry]") {
    using koalaamp::ui::formatKbps;
    using koalaamp::ui::formatKhz;
    CHECK(formatKbps(128) == "128");
    CHECK(formatKbps(64) == " 64");
    CHECK(formatKbps(8) == "  8");
    CHECK(formatKbps(1000) == "10H");
    CHECK(formatKbps(1440) == "14H");
    CHECK(formatKbps(12345) == " 1C");
    CHECK(formatKhz(44) == "44");
    CHECK(formatKhz(22) == "22");
    CHECK(formatKhz(8) == " 8");
    CHECK(formatKhz(192) == "92");
}

TEST_CASE("SE-2 title text folds into the uppercase 5x6 font", "[se2][title]") {
    using koalaamp::ui::foldForFont;
    using koalaamp::ui::marqueeDisplay;
    using koalaamp::ui::marqueeScrollPx;
    using koalaamp::ui::marqueeScrolls;
    // Lower case folds to the case-shared cell; accented letters fold to ASCII
    // (Webamp's diacritic table), 'ß' to its first folded character.
    CHECK(foldForFont(U'a') == U'a');
    CHECK(foldForFont(U'A') == U'a');
    CHECK(foldForFont(U'\u00C5') == U'a');  // Å -> A -> a
    CHECK(foldForFont(U'\u00DF') == U's');  // ß -> "ss" -> s
    CHECK(foldForFont(U'\u0301') == U'\uFFFD');

    // Short titles never scroll: padded to 31 cells.
    CHECK_FALSE(marqueeScrolls("KoalaAmp"));
    CHECK(marqueeDisplay("KoalaAmp").size() == 31);
    CHECK(marqueeScrollPx("KoalaAmp", 7) == 0);
    // 31 cells or more doubles with the separator and scrolls one cell per step.
    const std::string longTitle = "01. artist - a very long track title";
    CHECK(marqueeScrolls(longTitle));
    CHECK(marqueeDisplay(longTitle) == longTitle + "  ***  " + longTitle);
    CHECK(marqueeScrollPx(longTitle, 0) == 0);
    CHECK(marqueeScrollPx(longTitle, 1) == 5);
    CHECK(marqueeScrollPx(longTitle, 8) == 40);
    // Wrapping period: 5 * (cells + separator length).
    const int period = 5 * static_cast<int>(longTitle.size() + 7);
    CHECK(marqueeScrollPx(longTitle, period / 5) == 0);
    CHECK(marqueeScrollPx(longTitle, period / 5 + 3) == 15);
}

TEST_CASE("SE-2 frame hash is a stable SHA-256 over RGB triples", "[se2][hash]") {
    using koalaamp::ui::frameHash;
    QImage single(1, 1, QImage::Format_RGB32);
    single.fill(Qt::black);
    CHECK(frameHash(single) == "709e80c88487a2411e1ee4dfb9f22a861492d20c4765150c0c794abd70f8147c");
    QImage pair(2, 1, QImage::Format_RGB32);
    pair.setPixel(0, 0, qRgb(255, 0, 0));
    pair.setPixel(1, 0, qRgb(0, 255, 0));
    CHECK(frameHash(pair) == "0757241fa846855871c4ec6994954cbe0e0e7a8fd7bb3f7487462f6ce4215780");
    CHECK(frameHash(QImage()).empty());
}

TEST_CASE("SE-2 visualiser follows the Winamp/Winamp-JS rules", "[se2][vis]") {
    using namespace koalaamp::ui;
    // 75 bands of one pixel, a 1024-sample transform, and the decay constant
    // (12/16 px per frame = "moderate") recorded in the ticket.
    VisAnalyzer analyzer;
    REQUIRE(analyzer.config().falloff == 12);
    REQUIRE(analyzer.config().wideBandwidth);

    std::vector<float> silence(WinampFft::kSize, 0.0F);
    VisFrame frame;
    analyzer.analyze(silence.data(), silence.size(), frame);
    CHECK(frame.barsValid);
    CHECK(std::all_of(frame.bars.begin(), frame.bars.end(), [](std::uint8_t value) {
        return value == 0;
    }));
    CHECK(std::all_of(frame.peaks.begin(), frame.peaks.end(),
                      [](std::uint8_t value) { return value == kPeakOff; }));
    // Silence sits at row 7 (round(128/16*2) - 9); the renderer adds the
    // pushDown of 2 when it draws the column.
    CHECK(frame.osc[0] == 7);

    // A loud full-scale square wave must raise bars, and the peak markers must
    // stay at or above the bars.
    std::vector<float> loud(WinampFft::kSize);
    for (std::size_t i = 0; i < loud.size(); ++i) {
        loud[i] = (i / 32) % 2 == 0 ? 1.0F : -1.0F;
    }
    VisFrame loudFrame;
    analyzer.analyze(loud.data(), loud.size(), loudFrame);
    int lit = 0;
    for (int i = 0; i < kVisBars; ++i) {
        if (loudFrame.bars[static_cast<std::size_t>(i)] > 0) {
            ++lit;
        }
    }
    CHECK(lit > 0);
    // Decay: after one more silent frame every bar must be at most what it was.
    for (int step = 0; step < 3; ++step) {
        const auto before = loudFrame.bars;
        analyzer.analyze(silence.data(), silence.size(), loudFrame);
        for (int i = 0; i < kVisBars; ++i) {
            CHECK(loudFrame.bars[static_cast<std::size_t>(i)] <=
                  before[static_cast<std::size_t>(i)]);
        }
    }

    // The synthetic feed is deterministic and exercises every bar/peak/point.
    const VisFrame synthetic = syntheticVisFrame(4);
    CHECK(synthetic == syntheticVisFrame(4));
    CHECK(synthetic != syntheticVisFrame(5));
    CHECK(std::any_of(synthetic.bars.begin(), synthetic.bars.end(),
                      [](std::uint8_t value) { return value > 0; }));
    CHECK(std::any_of(synthetic.peaks.begin(), synthetic.peaks.end(),
                      [](std::uint8_t value) { return value != kPeakOff; }));
}

TEST_CASE("SE-2 PCM ring down-mixes and keeps the newest window", "[se2][vis]") {
    using namespace koalaamp::ui;
    PcmRing ring(2048);
    CHECK(ring.capacity() == 2048);
    const float stereo[] = {1.0F, -1.0F, 0.5F, 0.5F};  // 2 frames, 2 channels
    ring.push(stereo, 2, 2);
    float out[4] = {9.0F, 9.0F, 9.0F, 9.0F};
    REQUIRE(ring.readRecent(out, 4) == 2);
    CHECK(out[0] == 0.0F);  // (1 + -1) / 2
    CHECK(out[1] == 0.5F);
    // Full ring: the newest frames win and wrap correctly.
    std::vector<float> many(4096, 0.25F);
    ring.push(many.data(), many.size(), 1);
    float last[2] = {0.0F, 0.0F};
    REQUIRE(ring.readRecent(last, 2) == 2);
    CHECK(last[0] == 0.25F);
    CHECK(last[1] == 0.25F);
    ring.clear();
}

TEST_CASE("SE-2 main window renders 275x116 and shades to 275x14", "[se2][render][fixture]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    const PlayerState* base = koalaamp::ui::goldenState("default");
    REQUIRE(base != nullptr);
    const QImage full = render(*base);
    CHECK(full.size() == QSize(275, 116));
    PlayerState shaded = *base;
    shaded.shade = true;
    CHECK(render(shaded).size() == QSize(275, 14));
    // Deterministic: two renders of the same state are byte-identical.
    CHECK(koalaamp::ui::frameHash(full) == koalaamp::ui::frameHash(render(*base)));
}

TEST_CASE("SE-2 every control's state change is confined to its own rect",
          "[se2][render][fixture]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    using namespace koalaamp::ui;
    const PlayerState* basePtr = goldenState("default");
    REQUIRE(basePtr != nullptr);
    const PlayerState base = *basePtr;
    const auto L = [](QRect rect) { return std::vector<QRect>{rect}; };

    SECTION("volume bar and thumb") {
        PlayerState state = base;
        state.volume = 0;
        checkChangeConfined(base, state, {MainLayout::volume, MainLayout::volume});
    }
    SECTION("balance bar and thumb") {
        PlayerState state = base;
        state.balance = -100;
        checkChangeConfined(base, state, L(MainLayout::balance));
    }
    SECTION("seek bar thumb") {
        PlayerState state = base;
        state.status = PlayerStatus::Playing;
        state.position = 50;
        checkChangeConfined(base, state,
                            {MainLayout::position, MainLayout::playPause,
                             MainLayout::workIndicator, MainLayout::time, MainLayout::kbps,
                             MainLayout::khz, MainLayout::mono, MainLayout::stereo,
                             MainLayout::visualizer});
    }
    SECTION("shuffle and repeat") {
        PlayerState state = base;
        state.shuffle = true;
        checkChangeConfined(base, state, L(MainLayout::shuffle));
        PlayerState repeat = base;
        repeat.repeat = true;
        checkChangeConfined(base, repeat, L(MainLayout::repeat));
    }
    SECTION("transport buttons show their active sprite") {
        PlayerState state = base;
        state.active.next = true;
        checkChangeConfined(base, state, L(MainLayout::next));
    }
    SECTION("title bar active/inactive and the llama easter egg") {
        PlayerState inactive = base;
        inactive.selected = false;
        checkChangeConfined(base, inactive, L(MainLayout::titleBar));
        PlayerState llama = base;
        llama.llama = true;
        checkChangeConfined(base, llama, L(MainLayout::titleBar));
    }
    SECTION("clutter bar toggles") {
        PlayerState state = base;
        state.clutter.d = true;
        checkChangeConfined(base, state, L(QRect(MainLayout::clutterBar.x(),
                                                 MainLayout::clutterBar.y() + 25, 8, 8)));
    }
    SECTION("eq/playlist buttons: four states each") {
        PlayerState off = base;
        off.eqSelected = false;
        checkChangeConfined(base, off, L(MainLayout::eqButton));
        PlayerState pressed = base;
        pressed.eqDepressed = true;
        checkChangeConfined(base, pressed, L(MainLayout::eqButton));
        PlayerState pressedOff = off;
        pressedOff.eqDepressed = true;
        checkChangeConfined(off, pressedOff, L(MainLayout::eqButton));
    }
    SECTION("mono/stereo indicators") {
        PlayerState state = base;
        state.status = PlayerStatus::Playing;
        state.channels = 1;
        checkChangeConfined(base, state,
                            {MainLayout::mono, MainLayout::playPause,
                             MainLayout::workIndicator, MainLayout::time, MainLayout::kbps,
                             MainLayout::khz, MainLayout::visualizer, MainLayout::position});
    }
    SECTION("time display countdown minus sign") {
        PlayerState state = base;
        state.status = PlayerStatus::Playing;
        state.timeVisible = true;
        state.timeSeconds = 65;
        PlayerState countdown = state;
        countdown.countdown = true;
        checkChangeConfined(state, countdown,
                            {ShadeLayout::minusSign, MainLayout::time});
    }
    SECTION("windowshade swaps the whole window") {
        PlayerState state = base;
        state.shade = true;
        const QImage shaded = render(state);
        CHECK(shaded.size() == QSize(275, 14));
        // The shade bar is the titlebar's shaded art, not a squashed window.
        const QImage& titlebar = *fixtureSkin()->sheets.sheet("TITLEBAR");
        CHECK(shaded.pixel(0, 0) == titlebar.pixel(27, 29));
    }
}

TEST_CASE("SE-2 integer 2x is the exact nearest-neighbour double of 1x",
          "[se2][render][fixture]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    using namespace koalaamp::ui;
    for (const char* name : {"default", "spectrum", "shade", "playing"}) {
        const PlayerState* state = goldenState(name);
        REQUIRE(state != nullptr);
        const QImage one = render(*state, 1);
        const QImage two = render(*state, 2);
        REQUIRE(two.size() == QSize(one.width() * 2, one.height() * 2));
        bool exact = true;
        for (int y = 0; y < one.height() && exact; ++y) {
            for (int x = 0; x < one.width(); ++x) {
                const QRgb expected = one.pixel(x, y);
                if (two.pixel(x * 2, y * 2) != expected || two.pixel(x * 2 + 1, y * 2) != expected ||
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

TEST_CASE("SE-2 visualiser area honours the mode and the paused state",
          "[se2][render][fixture]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    using namespace koalaamp::ui;
    const QImage& mainSheet = *fixtureSkin()->sheets.sheet("MAIN");
    const auto visColor = [&](std::size_t index) {
        const auto& rgb = fixtureSkin()->visColor.colors[index];
        return qRgb(rgb.r, rgb.g, rgb.b);
    };
    const PlayerState* playing = goldenState("playing");
    REQUIRE(playing != nullptr);
    const QRect area = MainLayout::visualizer;

    // Off: the vis area keeps main.bmp's own art.
    PlayerState off = *playing;
    off.vis = VisMode::Off;
    const QImage offFrame = render(off);
    CHECK(offFrame.pixel(25, 43) == mainSheet.pixel(25, 43));
    CHECK(offFrame.pixel(30, 50) == mainSheet.pixel(30, 50));

    // Spectrum (silent): viscolor[0] fill with the viscolor[1] dither grid at
    // even x / odd y — and no bars, because silence has no energy.
    PlayerState silent = *playing;
    silent.visFrame.clear();
    silent.visFrame.barsValid = true;
    silent.visFrame.peaks.fill(kPeakOff);
    const QImage silentFrame = render(silent);
    CHECK(silentFrame.pixel(area.x() + 1, area.y()) == visColor(0));
    CHECK(silentFrame.pixel(area.x(), area.y() + 1) == visColor(1));
    CHECK(silentFrame.pixel(area.x() + 1, area.y() + 1) == visColor(0));

    // Bars paint from the bottom of the area: a full-height bar column reaches
    // the dither background's top row.
    PlayerState loud = silent;
    loud.visFrame.bars.fill(15);
    loud.visFrame.peaks.fill(kPeakOff);
    const QImage loudFrame = render(loud);
    CHECK(loudFrame.pixel(area.x() + 4, area.y() + 3) == visColor(3));
    CHECK(loudFrame.pixel(area.x() + 4, area.y() + 15) == visColor(15));

    // Paused keeps the last frame (Winamp stops painting but does not blank).
    PlayerState paused = loud;
    paused.status = PlayerStatus::Paused;
    const QImage pausedFrame = render(paused);
    CHECK(pausedFrame.pixel(area.x() + 4, area.y() + 15) == visColor(15));

    // Stopped: no canvas at all, so main.bmp shows through again.
    PlayerState stopped = loud;
    stopped.status = PlayerStatus::Stopped;
    const QImage stoppedFrame = render(stopped);
    CHECK(stoppedFrame.pixel(area.x() + 4, area.y() + 15) == mainSheet.pixel(area.x() + 4, area.y() + 15));
    (void)visColor;
}

TEST_CASE("SE-2 per-state golden frames match the committed manifest",
          "[se2][golden][fixture]") {
    if (fixtureSkin() == nullptr) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    const auto manifest = readManifest(KOALAAMP_SE2_GOLDEN_MANIFEST);
    INFO("manifest: " << KOALAAMP_SE2_GOLDEN_MANIFEST);
    REQUIRE_FALSE(manifest.empty());
    for (const auto& [name, state] : koalaamp::ui::goldenStateMatrix()) {
        for (const int scale : {1, 2}) {
            const std::string key = name + "@" + std::to_string(scale) + "x";
            const auto it = manifest.find(key);
            REQUIRE(it != manifest.end());
            const std::string digest = koalaamp::ui::frameHash(render(state, scale));
            INFO("state " << key);
            CHECK(digest == it->second);
        }
    }
    // No stale entries: every manifest line must correspond to a live state.
    std::set<std::string> expected;
    for (const auto& [name, state] : koalaamp::ui::goldenStateMatrix()) {
        (void)state;
        expected.insert(name + "@1x");
        expected.insert(name + "@2x");
    }
    for (const auto& [key, digest] : manifest) {
        (void)digest;
        CHECK(expected.count(key) == 1);
    }
}
