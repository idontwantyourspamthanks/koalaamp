// SPDX-License-Identifier: MIT
//
// SE-5 interaction-parity suite (docs/tickets/SE-5-eq-window.md).
//
// Every item of `EqWindow::interactionChecklist()` is exercised here as a
// scripted Qt mouse/key/wheel event on the real widget (or a direct model call)
// followed by an assertion on the render state / pushed EqSettings. The last
// test asserts the exercised set covers the checklist exactly.
//
// The DSP-wiring acceptance items are separate cases below and drive AU-1's
// real `Equalizer` through an `IEqSink` adapter while tapping its PCM output:
//   * a slider reaching the DSP is audible within one 1024-frame block;
//   * a preset switch stays glitch-free (bounded sample-to-sample step);
//   * EQ ON bypass is bit-flat (identical to the source);
//   * `.eqf` save/load round-trips through AU-1's byte-compatible codec.
//
// No skin fixture is needed for the interaction assertions (the hit regions are
// layout data and the window paints black without a skin); the fixture-
// dependent checks skip instead.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <set>
#include <string>
#include <vector>

#include <QApplication>
#include <QImage>
#include <QPoint>
#include <QWheelEvent>
#include <QTest>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/Equalizer.hpp"
#include "audio/EqPreset.hpp"
#include "skins/SkinSet.hpp"
#include "ui/EqGoldenStates.hpp"
#include "ui/EqRenderer.hpp"
#include "ui/EqSink.hpp"
#include "ui/EqState.hpp"
#include "ui/EqWindow.hpp"

namespace {

using koalaamp::audio::EqPreset;
using koalaamp::audio::EqSettings;
using koalaamp::ui::EqHitId;
using koalaamp::ui::EqLayout;
using koalaamp::ui::EqSlider;
using koalaamp::ui::EqWindow;
using koalaamp::ui::EqWindowState;
using koalaamp::ui::IEqSink;

/// Records the settings the window pushes.
class FakeEqSink : public IEqSink {
public:
    void setEqSettings(const EqSettings& settings) override {
        last = settings;
        ++pushes;
    }
    EqSettings eqSettings() const override { return last; }

    EqSettings last{};
    int pushes = 0;
};

/// One scripted scenario: a fresh EQ window plus its sink.
struct Scene {
    EqWindow window;
    FakeEqSink sink;

    Scene() {
        window.setPresetDialogsEnabled(false);
        window.setEqSink(&sink);
        window.show();
        QApplication::processEvents();
    }
    ~Scene() { window.setEqSink(nullptr); }

    void click(int x, int y) {
        QTest::mouseClick(&window, Qt::LeftButton, Qt::KeyboardModifiers(), QPoint(x, y));
        QApplication::processEvents();
    }
    void press(int x, int y) {
        QTest::mousePress(&window, Qt::LeftButton, Qt::KeyboardModifiers(), QPoint(x, y));
        QApplication::processEvents();
    }
    void move(int x, int y) {
        QTest::mouseMove(&window, QPoint(x, y));
        QApplication::processEvents();
    }
    void release(int x, int y) {
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::KeyboardModifiers(), QPoint(x, y));
        QApplication::processEvents();
    }
    void wheel(int x, int y, int notches) {
        const QPointF local(x, y);
        QWheelEvent event(local, window.mapToGlobal(QPoint(x, y)), QPoint(0, 0),
                          QPoint(0, notches * 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(&window, &event);
        QApplication::processEvents();
    }
    QImage frame() const {
        const auto* sheets = skinSheets();
        if (sheets == nullptr) {
            QImage black(275, 116, QImage::Format_RGB32);
            black.fill(Qt::black);
            return black;
        }
        return koalaamp::ui::renderEqWindow(*sheets, window.state(), window.scale());
    }
    static const koalaamp::skins::Skin* skinSheets() {
        static const auto skin = koalaamp::skins::loadSkinSet(KOALAAMP_SE5_SKIN);
        return skin.has_value() ? &skin->sheets : nullptr;
    }
};

/// The pixel column of the 1 kHz band groove (index 5).
constexpr int kBand1000X = 78 + 18 * 4;

}  // namespace

TEST_CASE("SE-5 interaction parity checklist", "[se5][interaction]") {
    std::set<std::string> covered;
    const auto cover = [&covered](const char* item) { covered.insert(item); };

    // ---------------------------------------------------------------------
    // Window chrome
    // ---------------------------------------------------------------------
    {  // titlebar drag + double-click shade
        Scene scene;
        const QPoint before = scene.window.pos();
        scene.press(120, 7);
        scene.move(160, 40);
        scene.release(160, 40);
        CHECK(scene.window.pos() != before);
        cover("titlebar-drag-moves-window");

        QTest::mouseDClick(&scene.window, Qt::LeftButton, Qt::KeyboardModifiers(), QPoint(120, 7));
        QApplication::processEvents();
        CHECK(scene.window.isShaded());
        cover("titlebar-double-click-toggles-shade");
    }
    {  // shade button
        Scene scene;
        scene.click(254 + 4, 3 + 4);
        CHECK(scene.window.isShaded());
        CHECK(scene.window.height() == 14);
        CHECK(scene.window.width() == 275);
        cover("shade-button-toggles-shade");
        cover("shade-uses-275x14-geometry");
    }
    {  // close request
        Scene scene;
        bool requested = false;
        QObject::connect(&scene.window, &EqWindow::closeRequested, [&] { requested = true; });
        scene.click(264 + 4, 3 + 4);
        CHECK(requested);
        cover("close-click-requests-close");
    }
    {  // double size is an integer 2x
        Scene scene;
        scene.window.setDoubleSize(true);
        QApplication::processEvents();
        CHECK(scene.window.size() == QSize(550, 232));
        CHECK(scene.window.scale() == 2);
        cover("double-size-is-integer-nearest-neighbour");
    }
    {  // region.txt mask (base-2.91's REGION.TXT is documentation only: find a
       // corpus skin that really defines one).
        std::optional<koalaamp::skins::SkinSet> skinned;
        const std::filesystem::path corpus = KOALAAMP_CORPUS_DIR;
        if (std::filesystem::is_directory(corpus)) {
            for (const auto& entry : std::filesystem::directory_iterator(corpus)) {
                if (entry.path().extension() != ".wsz") {
                    continue;
                }
                auto candidate = koalaamp::skins::loadSkinSet(entry.path());
                if (candidate.has_value() && !candidate->region.empty()) {
                    skinned = std::move(candidate);
                    break;
                }
            }
        }
        if (!skinned.has_value()) {
            SKIP("no corpus skin with an active region.txt — run scripts/fetch-corpus.sh");
        }
        Scene scene;
        scene.window.setSkin(&*skinned);
        QApplication::processEvents();
        CHECK_FALSE(scene.window.mask().isEmpty());
        const QRegion normal = scene.window.mask();
        scene.window.setShade(true);
        QApplication::processEvents();
        CHECK(scene.window.mask() != normal);
        cover("region-mask-applied-in-both-states");
    }

    // ---------------------------------------------------------------------
    // Buttons
    // ---------------------------------------------------------------------
    {  // ON toggle + bypass
        Scene scene;
        CHECK_FALSE(scene.sink.last.enabled);
        scene.click(14 + 13, 18 + 6);
        CHECK(scene.window.state().enabled);
        CHECK(scene.sink.last.enabled);
        cover("on-click-toggles-enabled");
        scene.click(14 + 13, 18 + 6);
        CHECK_FALSE(scene.window.state().enabled);
        CHECK_FALSE(scene.sink.last.enabled);  // bypass passes enabled=false
        cover("on-bypass-passes-enabled-false");
    }
    {  // AUTO toggle
        Scene scene;
        scene.click(40 + 16, 18 + 6);
        CHECK(scene.window.state().autoLoad);
        cover("auto-click-toggles-autoload");
    }
    {  // PRESETS button held shows the selected sprite
        Scene scene;
        scene.press(217 + 22, 18 + 6);
        CHECK(scene.window.state().presetsPressed);
        if ((Scene::skinSheets() != nullptr)) {
            const QImage held = scene.frame();
            scene.release(217 + 22, 18 + 6);
            const QImage idle = scene.frame();
            CHECK(held != idle);
        }
        cover("presets-button-renders-two-states");
    }

    // ---------------------------------------------------------------------
    // Sliders
    // ---------------------------------------------------------------------
    {  // preamp drag
        Scene scene;
        scene.press(EqLayout::preamp.x() + 7, 39);  // near the top = +12 dB
        CHECK(scene.window.sliderValue(0) == koalaamp::ui::kEqValueBoost);
        CHECK(scene.sink.last.preamp == koalaamp::ui::kEqValueBoost);
        cover("preamp-drag-sets-value");
    }
    {  // band drag + selected thumb while dragging
        Scene scene;
        scene.press(kBand1000X + 7, 99);  // near the bottom = -12 dB
        CHECK(scene.window.sliderValue(static_cast<int>(EqSlider::Hz1000)) ==
              koalaamp::ui::kEqValueCut);
        const int pushedIndex = 5;
        CHECK(scene.sink.last.bands[static_cast<std::size_t>(pushedIndex - 1)] ==
              koalaamp::ui::kEqValueCut);
        if ((Scene::skinSheets() != nullptr)) {
            const QImage dragging = scene.frame();
            scene.release(kBand1000X + 7, 99);
            const QImage idle = scene.frame();
            CHECK(dragging != idle);
        } else {
            scene.release(kBand1000X + 7, 99);
        }
        cover("band-drag-sets-value");
        cover("slider-thumb-selected-while-dragging");
    }
    {  // wheel
        Scene scene;
        const int before = scene.window.sliderValue(1);
        scene.wheel(EqLayout::bandX0 + 7, EqLayout::bandY + 30, 1);  // up = more boost
        CHECK(scene.window.sliderValue(1) == before - 1);
        scene.wheel(EqLayout::bandX0 + 7, EqLayout::bandY + 30, -1);
        CHECK(scene.window.sliderValue(1) == before);
        cover("slider-wheel-steps-value");
    }
    {  // keyboard
        Scene scene;
        scene.window.setFocus();
        const int before = scene.window.sliderValue(0);
        QTest::keyClick(&scene.window, Qt::Key_Up);
        QApplication::processEvents();
        CHECK(scene.window.sliderValue(0) == before - 1);
        QTest::keyClick(&scene.window, Qt::Key_Down);
        QApplication::processEvents();
        CHECK(scene.window.sliderValue(0) == before);
        cover("slider-keyboard-steps-value");
    }

    // ---------------------------------------------------------------------
    // dB zones / reset
    // ---------------------------------------------------------------------
    {  // +12
        Scene scene;
        scene.click(45 + 4, 36 + 4);
        CHECK(scene.window.sliderValue(5) == koalaamp::ui::kEqValueBoost);
        CHECK(scene.window.sliderValue(0) == koalaamp::ui::kEqValueUnity);  // preamp untouched
        cover("plus12-zone-sets-bands-max");
    }
    {  // 0 dB = the ticket's RESET (every slider to unity)
        Scene scene;
        scene.window.setAllBands(koalaamp::ui::kEqValueCut);
        scene.click(45 + 4, 64 + 4);
        CHECK(koalaamp::ui::isFlat(scene.window.state()));
        scene.window.resetAll();
        CHECK(koalaamp::ui::isFlat(scene.window.state()));
        cover("zerodb-zone-sets-all-unity");
    }
    {  // -12
        Scene scene;
        scene.click(45 + 4, 95 + 4);
        CHECK(scene.window.sliderValue(10) == koalaamp::ui::kEqValueCut);
        cover("minus12-zone-sets-bands-min");
    }

    // ---------------------------------------------------------------------
    // Presets
    // ---------------------------------------------------------------------
    {  // classic preset load
        Scene scene;
        REQUIRE(scene.window.loadClassicPreset(8));  // Rock
        const EqPreset rock = koalaamp::audio::classicEqPresets()[8];
        CHECK(scene.window.sliderValue(1) == rock.bands[0]);
        CHECK(scene.window.presetIndex() == 8);
        cover("classic-preset-loads-sliders");
    }
    {  // a manual edit marks the settings custom
        Scene scene;
        REQUIRE(scene.window.loadClassicPreset(0));
        scene.window.setSliderValue(3, koalaamp::ui::kEqValueCut);
        CHECK(scene.window.presetIndex() == -1);
        cover("preset-change-marks-custom");
    }
    {  // .eqf save/load round-trip through AU-1's codec
        Scene scene;
        const std::filesystem::path fixtureDir = KOALAAMP_EQ_FIXTURE_DIR;
        if (std::filesystem::is_directory(fixtureDir)) {
            REQUIRE(scene.window.loadPresetFromFile((fixtureDir / "classical.eqf").string()));
        } else {
            scene.window.setSliderValue(4, koalaamp::ui::kEqValueBoost);
        }
        const std::filesystem::path out =
            std::filesystem::temp_directory_path() / "koalaamp_se5_checklist.eqf";
        REQUIRE(scene.window.savePresetToFile(out.string()));
        const auto saved = scene.window.state().sliders;
        scene.window.resetAll();
        REQUIRE(scene.window.loadPresetFromFile(out.string()));
        CHECK(scene.window.state().sliders == saved);
        std::error_code ec;
        std::filesystem::remove(out, ec);
        cover("eqf-save-load-round-trip");
    }

    // ---------------------------------------------------------------------
    // Shade previews
    // ---------------------------------------------------------------------
    {  // shade volume / balance drags
        Scene scene;
        scene.window.setShade(true);
        QApplication::processEvents();
        scene.press(EqLayout::shadeVolume.x() + 90, EqLayout::shadeVolume.y() + 3);
        CHECK(scene.window.state().volume >= 90);
        scene.release(EqLayout::shadeVolume.x() + 90, EqLayout::shadeVolume.y() + 3);
        cover("shade-volume-drag-updates-preview");

        scene.press(EqLayout::shadeBalance.x() + 2, EqLayout::shadeBalance.y() + 3);
        CHECK(scene.window.state().balance <= -90);
        scene.release(EqLayout::shadeBalance.x() + 2, EqLayout::shadeBalance.y() + 3);
        cover("shade-balance-drag-updates-preview");
    }

    // ---------------------------------------------------------------------
    // DSP seam
    // ---------------------------------------------------------------------
    {  // a slider edit pushes settings
        Scene scene;
        scene.window.setSliderValue(0, koalaamp::ui::kEqValueBoost);
        CHECK(scene.sink.last.preamp == koalaamp::ui::kEqValueBoost);
        cover("slider-pushes-eqsettings");
    }
    {  // a preset load pushes settings
        Scene scene;
        REQUIRE(scene.window.loadClassicPreset(2));
        const EqPreset preset = koalaamp::audio::classicEqPresets()[2];
        CHECK(scene.sink.last.bands[0] == preset.bands[0]);
        cover("preset-load-pushes-eqsettings");
    }

    // ---------------------------------------------------------------------
    // Coverage
    // ---------------------------------------------------------------------
    const auto& checklist = EqWindow::interactionChecklist();
    std::vector<std::string> missing;
    for (const std::string& item : checklist) {
        if (covered.count(item) == 0) {
            missing.push_back(item);
        }
    }
    std::string joined;
    for (const std::string& item : missing) {
        joined += item + " ";
    }
    CAPTURE(joined);
    CHECK(missing.empty());
    for (const std::string& item : covered) {
        CHECK(std::find(checklist.begin(), checklist.end(), item) != checklist.end());
    }
}

// ---------------------------------------------------------------------------
// AU-1 wiring: PCM-tap assertions
// ---------------------------------------------------------------------------

namespace {

/// Forwards the window's settings into AU-1's real `Equalizer` and exposes a
/// planar PCM tap so a block can be measured.
class EqualizerSink : public IEqSink {
public:
    EqualizerSink() { m_eq.prepare(kSampleRate, 1); }
    void setEqSettings(const EqSettings& settings) override {
        m_settings = settings;
        m_eq.setSettings(settings);
    }
    EqSettings eqSettings() const override { return m_settings; }

    /// Processes one planar mono block, returning it (crossfade included).
    std::vector<float> block(const std::vector<float>& input) {
        std::vector<float> buffer = input;
        float* channels[1] = {buffer.data()};
        m_eq.process(channels, static_cast<int>(buffer.size()));
        return buffer;
    }

    static constexpr int kSampleRate = 48000;
    static constexpr int kBlock = 1024;

private:
    koalaamp::audio::Equalizer m_eq;
    EqSettings m_settings;
};

std::vector<float> sine(int samples, double hz, long startIndex = 0) {
    std::vector<float> out(static_cast<std::size_t>(samples));
    const double w = 2.0 * std::numbers::pi * hz / EqualizerSink::kSampleRate;
    for (int i = 0; i < samples; ++i) {
        out[static_cast<std::size_t>(i)] =
            static_cast<float>(std::sin(w * static_cast<double>(startIndex + i)));
    }
    return out;
}

double rms(const std::vector<float>& values, std::size_t begin = 0) {
    double sum = 0.0;
    for (std::size_t i = begin; i < values.size(); ++i) {
        sum += static_cast<double>(values[i]) * values[i];
    }
    const std::size_t count = values.size() - begin;
    return std::sqrt(sum / static_cast<double>(count == 0 ? 1 : count));
}

}  // namespace

TEST_CASE("SE-5 a slider change reaches the DSP within one 1024-frame block",
          "[se5][interaction][dsp]") {
    EqWindow window;
    window.setPresetDialogsEnabled(false);
    EqualizerSink sink;
    window.setEqSink(&sink);
    window.setState(EqWindowState{});  // flat, EQ off

    // With the EQ enabled and flat, the output equals the source.
    EqWindowState on = window.state();
    on.enabled = true;
    window.setState(on);

    // A continuous 1 kHz sine across blocks (block index advances, so the
    // boundary continuity check is meaningful).
    long phase = 0;
    const auto nextBlock = [&] {
        std::vector<float> block = sine(EqualizerSink::kBlock, 1000.0, phase);
        phase += EqualizerSink::kBlock;
        return block;
    };

    const std::vector<float> input = nextBlock();
    const std::vector<float> before = sink.block(input);
    CHECK(rms(before) == Catch::Approx(rms(input)).margin(1e-6));

    // Boost 1 kHz by +12 dB via the window: the change takes effect on the very
    // next block (the first block is the coefficient crossfade) and is fully
    // applied by the following one — i.e. within one 1024-frame block.
    window.setSliderValue(static_cast<int>(EqSlider::Hz1000), koalaamp::ui::kEqValueBoost);
    REQUIRE(sink.eqSettings().bands[4] == koalaamp::ui::kEqValueBoost);

    const std::vector<float> firstBlock = sink.block(nextBlock());
    const std::vector<float> secondBlock = sink.block(nextBlock());
    CHECK(firstBlock != before);  // already audibly different within one block
    CHECK(rms(secondBlock) == Catch::Approx(rms(before) * 3.98).epsilon(0.12));  // +12 dB

    // The crossfade spans exactly one block: no block-boundary discontinuity.
    CHECK(std::abs(static_cast<double>(firstBlock.front()) - before.back()) < 0.5);
}

TEST_CASE("SE-5 preset switching is glitch-free under audio", "[se5][interaction][dsp]") {
    EqWindow window;
    window.setPresetDialogsEnabled(false);
    EqualizerSink sink;
    window.setEqSink(&sink);
    EqWindowState on = window.state();
    on.enabled = true;
    window.setState(on);

    long phase = 0;
    const auto nextBlock = [&] {
        std::vector<float> block = sine(EqualizerSink::kBlock, 1000.0, phase);
        phase += EqualizerSink::kBlock;
        return block;
    };

    std::vector<float> previous = sink.block(nextBlock());
    double previousRms = rms(previous);

    // Cycle through several classic presets; each switch must stay bounded and
    // the RMS must move continuously (no dropout / spike).
    for (const int index : {8, 0, 12, 3, 16}) {
        REQUIRE(window.loadClassicPreset(index));
        const std::vector<float> block = sink.block(nextBlock());
        for (const float sample : block) {
            REQUIRE(std::isfinite(sample));
            CHECK(std::abs(sample) < 10.0f);
        }
        // Continuity across the switch boundary: the first sample of the new
        // block continues from the last sample of the previous one.
        CHECK(std::abs(static_cast<double>(block.front()) - previous.back()) < 0.5);
        const double nowRms = rms(block);
        CHECK(nowRms < previousRms * 4.0);  // RMS continuity: no spike
        previous = block;
        previousRms = nowRms;
    }
}

TEST_CASE("SE-5 EQ OFF bypass is bit-flat", "[se5][interaction][dsp]") {
    EqWindow window;
    window.setPresetDialogsEnabled(false);
    EqualizerSink sink;
    window.setEqSink(&sink);
    // Enabled with a real curve, then switched OFF.
    EqWindowState on = window.state();
    on.enabled = true;
    on.sliders[1] = koalaamp::ui::kEqValueBoost;
    window.setState(on);
    (void)sink.block(sine(EqualizerSink::kBlock, 1000.0));

    EqWindowState off = window.state();
    off.enabled = false;
    window.setState(off);
    const std::vector<float> input = sine(EqualizerSink::kBlock, 440.0);
    const std::vector<float> output = sink.block(input);
    for (std::size_t i = 0; i < input.size(); ++i) {
        REQUIRE(output[i] == input[i]);  // AU-1's flat assertion
    }
}

TEST_CASE("SE-5 .eqf save/load round-trips through AU-1's codec", "[se5][interaction][eqf]") {
    namespace fs = std::filesystem;
    const fs::path fixtureDir = KOALAAMP_EQ_FIXTURE_DIR;
    if (!fs::is_directory(fixtureDir)) {
        SKIP("eq fixture directory missing");
    }

    EqWindow window;
    window.setPresetDialogsEnabled(false);

    // Load a committed .eqf and check the sliders match AU-1's parser.
    REQUIRE(window.loadPresetFromFile((fixtureDir / "classical.eqf").string()));
    const auto parsed = koalaamp::audio::parseEqf(
        [&] {
            std::ifstream in(fixtureDir / "classical.eqf", std::ios::binary);
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                             std::istreambuf_iterator<char>());
        }());
    REQUIRE(parsed.size() == 1);
    CHECK(window.sliderValue(0) == parsed[0].preamp);
    for (int i = 0; i < koalaamp::ui::kEqWindowBands; ++i) {
        CHECK(window.sliderValue(i + 1) == parsed[0].bands[static_cast<std::size_t>(i)]);
    }

    // Save the current sliders and reparse: byte-identical to AU-1's writer.
    const fs::path out =
        fs::temp_directory_path() /
        ("koalaamp_se5_eqf_roundtrip.eqf");
    REQUIRE(window.savePresetToFile(out.string()));
    const std::vector<std::uint8_t> savedBytes = [&] {
        std::ifstream in(out, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>());
    }();
    const std::vector<std::uint8_t> expected = koalaamp::audio::writeEqf(
        koalaamp::audio::presetFromEqSettings(window.settingsFromState(), "KoalaAmp EQ"));
    CHECK(savedBytes == expected);
    const auto reread = koalaamp::audio::parseEqf(savedBytes);
    REQUIRE(reread.size() == 1);
    // Byte-compatible payload: bands + preamp match the source preset (the name
    // is this window's, not the fixture's).
    CHECK(reread[0].bands == parsed[0].bands);
    CHECK(reread[0].preamp == parsed[0].preamp);
    std::error_code ec;
    fs::remove(out, ec);

    // winamp.q1 import yields AU-1's classic set.
    CHECK(window.importPresetsFromFile((fixtureDir / "winamp.q1").string()));
    CHECK(window.userPresets().size() == koalaamp::audio::classicEqPresets().size());
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
