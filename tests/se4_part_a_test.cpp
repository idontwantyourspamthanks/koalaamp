// SPDX-License-Identifier: MIT
//
// SE-4 Part A — the assembled application window set.
//
// Proves the app-level assembly this ticket adds:
//   * the platform-launch decision (Wayland → XWayland, explicit choice wins);
//   * `WindowSet` routes the main window's EQ/PL toggles and the secondary
//     close buttons (the EQ window is live, not dead code);
//   * an EQ slider reaches AU-1's real DSP output within one 1024-frame block,
//     through the production `DspEqSink` and with the DSP installed ahead of the
//     vis feed exactly as `main.cpp` builds it;
//   * all three windows co-exist and render with a skin offscreen.
//
// Widget cases need a platform plugin; run under QT_QPA_PLATFORM=offscreen
// (like SE-2/SE-3/SE-5).
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <numbers>
#include <vector>

#include <QApplication>
#include <QImage>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "audio/DspPipeline.hpp"
#include "skins/SkinSet.hpp"
#include "ui/DspEqSink.hpp"
#include "ui/EqSink.hpp"
#include "ui/EqState.hpp"
#include "ui/EqWindow.hpp"
#include "ui/MainWindow.hpp"
#include "ui/PlaylistWindow.hpp"
#include "ui/VisFeed.hpp"
#include "ui/WindowPlatform.hpp"
#include "ui/WindowSet.hpp"

namespace {

namespace audio = koalaamp::audio;
namespace playback = koalaamp::playback;
namespace ui = koalaamp::ui;

constexpr int kSampleRate = 48000;
constexpr int kBlock = 1024;

std::vector<float> sine(int samples, double hz, long startIndex = 0) {
    std::vector<float> out(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        out[static_cast<std::size_t>(i)] = static_cast<float>(
            std::sin(2.0 * std::numbers::pi * hz / kSampleRate *
                     static_cast<double>(startIndex + i)));
    }
    return out;
}

double rms(const std::vector<float>& values) {
    if (values.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const float v : values) {
        sum += static_cast<double>(v) * v;
    }
    return std::sqrt(sum / static_cast<double>(values.size()));
}

}  // namespace

TEST_CASE("SE-4 launch requests XWayland only on a Wayland session", "[se4][platform]") {
    using ui::LaunchPlatform;
    // Wayland session, nothing explicit → XWayland (the app cannot self-place).
    CHECK(ui::launchPlatformPreference("", "wayland-0", "wayland") ==
          LaunchPlatform::RequestXWayland);
    CHECK(ui::launchPlatformPreference(nullptr, nullptr, "wayland") ==
          LaunchPlatform::RequestXWayland);
    // An explicit platform (xcb/offscreen/wayland) always wins.
    CHECK(ui::launchPlatformPreference("offscreen", "wayland-0", "wayland") ==
          LaunchPlatform::KeepCurrent);
    CHECK(ui::launchPlatformPreference("wayland", "wayland-0", "wayland") ==
          LaunchPlatform::KeepCurrent);
    // X11 / headless pass through.
    CHECK(ui::launchPlatformPreference("", nullptr, "x11") == LaunchPlatform::KeepCurrent);
    CHECK(ui::launchPlatformPreference(nullptr, nullptr, nullptr) ==
          LaunchPlatform::KeepCurrent);
}

TEST_CASE("SE-4 window set routes the EQ and playlist toggles", "[se4][assembly]") {
    ui::MainWindow main;
    ui::EqWindow eq;
    ui::PlaylistWindow playlist;
    eq.setPresetDialogsEnabled(false);
    playlist.setJumpDialogEnabled(false);
    ui::WindowSet set(main, eq, playlist);
    set.attach();
    set.showAll();
    QApplication::processEvents();

    CHECK(main.isVisible());
    CHECK(eq.isVisible());
    CHECK(playlist.isVisible());

    // The main window's EQ toggle drives EQ visibility (SE-5's window is live).
    main.eqToggled(false);
    QApplication::processEvents();
    CHECK_FALSE(eq.isVisible());
    main.eqToggled(true);
    QApplication::processEvents();
    CHECK(eq.isVisible());

    // The playlist toggle mirrors it (SE-3 pattern, now owned by the set).
    main.playlistToggled(false);
    QApplication::processEvents();
    CHECK_FALSE(playlist.isVisible());
    main.playlistToggled(true);
    QApplication::processEvents();
    CHECK(playlist.isVisible());

    // Each secondary window's own close button hides it.
    eq.closeRequested();
    QApplication::processEvents();
    CHECK_FALSE(eq.isVisible());
}

TEST_CASE("SE-4 an EQ slider reaches the DSP output within one block", "[se4][dsp]") {
    audio::DspPipeline dsp;
    ui::DspEqSink sink(dsp);
    ui::EqWindow eq;
    eq.setPresetDialogsEnabled(false);
    eq.setEqSink(&sink);

    // EQ ON at unity: the DSP output equals the source.
    ui::EqWindowState on = eq.state();
    on.enabled = true;
    eq.setState(on);
    REQUIRE(sink.eqSettings().enabled);

    std::vector<float> tap;
    auto chain = dsp.makeCallback(
        [&tap](const float* p, std::size_t frames, const playback::PcmFormat&) {
            tap.assign(p, p + frames);
        });

    long phase = 0;
    const auto run = [&] {
        std::vector<float> block = sine(kBlock, 1000.0, phase);
        phase += kBlock;
        chain(block.data(), block.size(), playback::PcmFormat{kSampleRate, 1});
        return block;
    };

    const std::vector<float> input = run();
    REQUIRE(tap.size() == input.size());
    CHECK(rms(tap) == Catch::Approx(rms(input)).margin(1e-6));

    // Boost 1 kHz by +12 dB: the change is audible on the very next block (the
    // coefficient crossfade spans one block) and fully applied by the one after.
    eq.setSliderValue(static_cast<int>(ui::EqSlider::Hz1000), ui::kEqValueBoost);
    REQUIRE(sink.eqSettings().bands[4] == ui::kEqValueBoost);

    const std::vector<float> first = run();
    CHECK(rms(tap) != Catch::Approx(rms(input)).margin(1e-9));
    run();
    CHECK(rms(tap) == Catch::Approx(rms(input) * 3.98).epsilon(0.12));
    (void)first;
}

TEST_CASE("SE-4 the DSP is installed ahead of the vis feed", "[se4][dsp][chain]") {
    // Mirrors main.cpp's construction: the tap installs
    // dsp.makeCallback(feed.callback()), so the feed's callback runs *after*
    // the transform. Feeding the chain must therefore populate the feed's
    // format fields.
    audio::DspPipeline dsp;
    ui::VisFeed feed;
    auto chain = dsp.makeCallback(feed.callback());

    std::vector<float> block = sine(kBlock, 440.0);
    chain(block.data(), block.size(), playback::PcmFormat{44100, 2});

    CHECK(feed.sampleRate() == 44100);
    CHECK(feed.channels() == 2);
}

TEST_CASE("SE-4 all three windows render with a skin offscreen", "[se4][assembly][render]") {
    const std::filesystem::path skinPath = KOALAAMP_SE4_SKIN;
    if (!std::filesystem::exists(skinPath)) {
        SKIP("skin fixture missing");
    }
    const auto skin = koalaamp::skins::loadSkinSet(skinPath);
    REQUIRE(skin.has_value());

    ui::MainWindow main;
    ui::EqWindow eq;
    ui::PlaylistWindow playlist;
    eq.setPresetDialogsEnabled(false);
    main.setSkin(&*skin);
    eq.setSkin(&*skin);
    playlist.setSkin(&*skin);

    ui::WindowSet set(main, eq, playlist);
    set.attach();
    set.showAll();
    QApplication::processEvents();

    CHECK(main.size() == QSize(ui::MainWindow::kWidth, ui::MainWindow::kHeight));
    CHECK(eq.size() == QSize(ui::EqWindow::kWidth, ui::EqWindow::kHeight));
    CHECK(!main.grab().isNull());
    CHECK(!eq.grab().isNull());
    CHECK(!playlist.grab().isNull());
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
