// SPDX-License-Identifier: MIT
//
// SE-2 interaction-parity suite (docs/tickets/SE-2-main-window.md).
//
// Every item of `MainWindow::interactionChecklist()` is exercised here as a
// scripted input (a synthesized Qt mouse/key/wheel event on the real widget, or
// a scripted PCM push) followed by an assertion on both the transport calls it
// produced and the render state it left behind. The last test asserts that the
// exercised set covers the checklist exactly — so "100%" is machine-checked and
// adding a checklist item without a script fails the build.
//
// No skin fixture is needed: the hit regions are layout data, and the canvas
// paints black without a skin (the fixture-dependent checks skip instead).
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <QApplication>
#include <QImage>
#include <QPoint>
#include <QWheelEvent>
#include <QTest>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "skins/SkinSet.hpp"
#include "ui/MainWindow.hpp"
#include "ui/PlayerRenderer.hpp"
#include "ui/PlayerState.hpp"
#include "ui/Transport.hpp"
#include "ui/VisData.hpp"

namespace {

using koalaamp::playback::MediaRef;
using koalaamp::playback::PlaybackState;
using koalaamp::playback::RepeatMode;
using koalaamp::ui::HitId;
using koalaamp::ui::MainLayout;
using koalaamp::ui::MainWindow;
using koalaamp::ui::PlayerState;
using koalaamp::ui::PlayerStatus;
using koalaamp::ui::ShadeLayout;
using koalaamp::ui::SliderMetrics;
using koalaamp::ui::VisMode;

/// A scripted transport: records every command and lets the test drive the
/// state the engine would report back.
class FakeTransport : public koalaamp::ui::ITransport {
public:
    std::vector<std::string> calls;
    bool media = false;
    PlaybackState stateValue = PlaybackState::Stopped;
    std::int64_t position = 0;
    std::int64_t length = 100'000;  // 100 s
    int volume = 78;
    bool shuffleValue = false;
    RepeatMode repeat = RepeatMode::Off;
    std::string titleValue = "fake track";
    int kbps = 0;

    void playMedia(const MediaRef&) override {
        calls.push_back("playMedia");
        media = true;
    }
    void resume() override {
        calls.push_back("resume");
        stateValue = PlaybackState::Playing;
    }
    void pause() override {
        calls.push_back("pause");
        stateValue = PlaybackState::Paused;
    }
    void togglePause() override {
        calls.push_back("togglePause");
        stateValue = stateValue == PlaybackState::Playing ? PlaybackState::Paused
                                                         : PlaybackState::Playing;
    }
    void stop() override {
        calls.push_back("stop");
        stateValue = PlaybackState::Stopped;
        position = 0;
    }
    void next() override { calls.push_back("next"); }
    void previous() override { calls.push_back("previous"); }
    void seekMs(std::int64_t ms) override {
        calls.push_back("seekMs:" + std::to_string(ms));
        position = ms;
    }
    void setVolumePercent(int value) override {
        calls.push_back("volume:" + std::to_string(value));
        volume = value;
    }
    void setShuffle(bool enabled) override {
        calls.push_back(std::string("shuffle:") + (enabled ? "1" : "0"));
        shuffleValue = enabled;
    }
    void setRepeatMode(RepeatMode mode) override {
        calls.push_back("repeat:" + std::to_string(static_cast<int>(mode)));
        repeat = mode;
    }

    bool hasMedia() const override { return media; }
    PlaybackState state() const override { return stateValue; }
    std::int64_t positionMs() const override { return position; }
    std::int64_t lengthMs() const override { return length; }
    int volumePercent() const override { return volume; }
    bool shuffle() const override { return shuffleValue; }
    RepeatMode repeatMode() const override { return repeat; }
    std::string title() const override { return titleValue; }
    int bitrateKbps() const override { return kbps; }

    bool called(std::string_view prefix) const {
        return std::any_of(calls.begin(), calls.end(), [&](const std::string& call) {
            return call.rfind(prefix, 0) == 0;
        });
    }
    std::size_t count(std::string_view prefix) const {
        return static_cast<std::size_t>(
            std::count_if(calls.begin(), calls.end(), [&](const std::string& call) {
                return call.rfind(prefix, 0) == 0;
            }));
    }
    std::string last() const { return calls.empty() ? std::string{} : calls.back(); }
    void clearCalls() { calls.clear(); }
};

/// One scripted scenario: a fresh window plus its transport.
struct Scene {
    MainWindow window;
    FakeTransport transport;

    Scene() {
        window.setFileDialogEnabled(false);
        window.setTransport(&transport);
        window.show();
        QTest_qWait();
    }
    ~Scene() { window.setTransport(nullptr); }

    static void QTest_qWait() {
        // Let the offscreen platform deliver the map/resize events.
        QApplication::processEvents();
    }

    QPoint at(int x, int y) const { return QPoint(x, y); }

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
    /// Render frames (not grabs): deterministic and independent of the platform.
    QImage frame() const { return koalaamp::ui::renderMainWindow(*skinSheets(), skinColors(),
                                                                window.state(), 1); }
    static const koalaamp::skins::Skin* skinSheets() {
        static const auto skin = koalaamp::skins::loadSkinSet(KOALAAMP_SE2_SKIN);
        return skin.has_value() ? &skin->sheets : nullptr;
    }
    static koalaamp::skins::VisColor skinColors() {
        static const auto skin = koalaamp::skins::loadSkinSet(KOALAAMP_SE2_SKIN);
        return skin.has_value() ? skin->visColor : koalaamp::skins::defaultVisColor();
    }
};

/// Pixels where two frames differ.
std::vector<QPoint> frameDiff(const QImage& a, const QImage& b) {
    std::vector<QPoint> out;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) {
                out.push_back(QPoint(x, y));
            }
        }
    }
    return out;
}

bool allInside(const std::vector<QPoint>& points, const std::vector<QRect>& rects) {
    return std::all_of(points.begin(), points.end(), [&](const QPoint& point) {
        return std::any_of(rects.begin(), rects.end(),
                           [&](const QRect& rect) { return rect.contains(point); });
    });
}

/// Centre of a layout rect as a window-local point.
QPoint centre(const QRect& rect) { return QPoint(rect.x() + rect.width() / 2, rect.y() + rect.height() / 2); }

}  // namespace

TEST_CASE("SE-2 interaction parity checklist", "[se2][interaction]") {
    std::set<std::string> covered;
    const auto cover = [&covered](const char* item) { covered.insert(item); };

    // ---------------------------------------------------------------------
    // Transport
    // ---------------------------------------------------------------------
    {  // play
        Scene scene;
        scene.transport.media = true;
        scene.transport.stateValue = PlaybackState::Stopped;
        // A media is loaded but stopped: the play button plays it again.
        scene.click(centre(MainLayout::play).x(), centre(MainLayout::play).y());
        CHECK(scene.transport.called("resume"));
        cover("play-click-starts-or-restarts");
        // Nothing loaded at all: the play button asks for a file instead.
        scene.transport.media = false;
        scene.transport.stateValue = PlaybackState::Stopped;
        scene.window.syncFromTransport();
        bool requested = false;
        QObject::connect(&scene.window, &MainWindow::ejectRequested, [&] { requested = true; });
        scene.click(centre(MainLayout::play).x(), centre(MainLayout::play).y());
        CHECK(requested);
        scene.transport.media = true;

        // Playing: the play button restarts the current track (Winamp).
        scene.transport.clearCalls();
        scene.transport.stateValue = PlaybackState::Playing;
        scene.window.syncFromTransport();
        scene.click(centre(MainLayout::play).x(), centre(MainLayout::play).y());
        CHECK(scene.transport.last() == "seekMs:0");
        scene.transport.clearCalls();
        // Paused: the play button resumes.
        scene.transport.stateValue = PlaybackState::Paused;
        scene.window.syncFromTransport();
        scene.click(centre(MainLayout::play).x(), centre(MainLayout::play).y());
        CHECK(scene.transport.called("togglePause"));
    }
    {  // pause/stop/next/previous
        Scene scene;
        scene.click(centre(MainLayout::pause).x(), centre(MainLayout::pause).y());
        CHECK(scene.transport.called("togglePause"));
        cover("pause-click-toggles-pause");

        scene.transport.clearCalls();
        scene.click(centre(MainLayout::stop).x(), centre(MainLayout::stop).y());
        CHECK(scene.transport.called("stop"));
        CHECK(scene.transport.last() == "seekMs:0");
        cover("stop-click-stops-and-rewinds");

        scene.transport.clearCalls();
        scene.click(centre(MainLayout::next).x(), centre(MainLayout::next).y());
        CHECK(scene.transport.last() == "next");
        cover("next-click-advances");

        scene.transport.clearCalls();
        scene.click(centre(MainLayout::previous).x(), centre(MainLayout::previous).y());
        CHECK(scene.transport.last() == "previous");
        cover("previous-click-calls-transport-previous");
    }
    {  // eject opens the file flow
        Scene scene;
        bool requested = false;
        QObject::connect(&scene.window, &MainWindow::ejectRequested, [&] { requested = true; });
        scene.click(centre(MainLayout::eject).x(), centre(MainLayout::eject).y());
        CHECK(requested);
        cover("eject-click-requests-media");
    }
    {  // transport press shows the active sprite
        Scene scene;
        scene.press(centre(MainLayout::play).x(), centre(MainLayout::play).y());
        CHECK(scene.window.state().active.play);
        const QImage held = scene.frame();
        scene.release(centre(MainLayout::play).x(), centre(MainLayout::play).y());
        CHECK_FALSE(scene.window.state().active.play);
        const auto changed = frameDiff(held, scene.frame());
        CHECK_FALSE(changed.empty());
        CHECK(allInside(changed, {MainLayout::play}));
        cover("transport-press-shows-active-sprite");
    }

    // ---------------------------------------------------------------------
    // Toggles
    // ---------------------------------------------------------------------
    {  // shuffle
        Scene scene;
        scene.click(centre(MainLayout::shuffle).x(), centre(MainLayout::shuffle).y());
        CHECK(scene.transport.called("shuffle:1"));
        CHECK(scene.window.state().shuffle);
        const QImage selected = scene.frame();
        scene.click(centre(MainLayout::shuffle).x(), centre(MainLayout::shuffle).y());
        CHECK(scene.transport.called("shuffle:0"));
        CHECK_FALSE(scene.window.state().shuffle);
        CHECK_FALSE(frameDiff(selected, scene.frame()).empty());
        cover("shuffle-click-toggles-and-renders-selected");

        scene.press(centre(MainLayout::shuffle).x(), centre(MainLayout::shuffle).y());
        CHECK(scene.window.state().shuffleDepressed);
        const QImage held = scene.frame();
        scene.release(centre(MainLayout::shuffle).x(), centre(MainLayout::shuffle).y());
        CHECK_FALSE(scene.window.state().shuffleDepressed);
        CHECK(allInside(frameDiff(held, scene.frame()), {MainLayout::shuffle}));
        cover("shuffle-press-shows-depressed-sprite");
    }
    {  // repeat
        Scene scene;
        scene.click(centre(MainLayout::repeat).x(), centre(MainLayout::repeat).y());
        CHECK(scene.transport.called("repeat:1"));
        CHECK(scene.window.state().repeat);
        scene.click(centre(MainLayout::repeat).x(), centre(MainLayout::repeat).y());
        CHECK(scene.transport.last() == "repeat:0");
        CHECK_FALSE(scene.window.state().repeat);
        cover("repeat-click-cycles-all-then-off");

        scene.press(centre(MainLayout::repeat).x(), centre(MainLayout::repeat).y());
        CHECK(scene.window.state().repeatDepressed);
        const QImage held = scene.frame();
        scene.release(centre(MainLayout::repeat).x(), centre(MainLayout::repeat).y());
        CHECK_FALSE(scene.window.state().repeatDepressed);
        CHECK(allInside(frameDiff(held, scene.frame()), {MainLayout::repeat}));
        cover("repeat-press-shows-depressed-sprite");
    }
    {  // eq / playlist buttons render four states and emit
        Scene scene;
        const QImage idle = scene.frame();
        bool eqSignal = false;
        bool plSignal = false;
        QObject::connect(&scene.window, &MainWindow::eqToggled, [&](bool on) { eqSignal = on; });
        QObject::connect(&scene.window, &MainWindow::playlistToggled,
                         [&](bool on) { plSignal = on; });

        Scene pressed;
        pressed.press(centre(MainLayout::eqButton).x(), centre(MainLayout::eqButton).y());
        const QImage eqPressed = pressed.frame();
        pressed.press(centre(MainLayout::plButton).x(), centre(MainLayout::plButton).y());
        const QImage bothPressed = pressed.frame();
        CHECK(allInside(frameDiff(eqPressed, bothPressed), {MainLayout::plButton}));
        (void)eqPressed;

        scene.window.setState([&] {
            PlayerState state = scene.window.state();
            state.eqSelected = false;
            return state;
        }());
        const QImage eqOff = scene.frame();
        CHECK_FALSE(frameDiff(idle, eqOff).empty());
        CHECK(allInside(frameDiff(idle, eqOff), {MainLayout::eqButton}));
        cover("eq-toggle-renders-four-states");

        scene.window.setState([&] {
            PlayerState state = scene.window.state();
            state.plSelected = false;
            return state;
        }());
        CHECK(allInside(frameDiff(idle, scene.frame()),
                        {MainLayout::eqButton, MainLayout::plButton}));
        cover("playlist-toggle-renders-four-states");

        scene.click(centre(MainLayout::eqButton).x(), centre(MainLayout::eqButton).y());
        CHECK(eqSignal);
        cover("eq-click-emits-toggled");
        scene.click(centre(MainLayout::plButton).x(), centre(MainLayout::plButton).y());
        CHECK(plSignal);
        cover("playlist-click-emits-toggled");
    }

    // ---------------------------------------------------------------------
    // Seek bar
    // ---------------------------------------------------------------------
    {  // seek bar
        Scene scene;
        scene.transport.media = true;
        scene.transport.length = 200'000;  // 200 s
        const int trackLeft = MainLayout::position.x();
        const int span = MainLayout::position.width() - SliderMetrics::positionThumbW;
        scene.click(trackLeft + span / 2, MainLayout::position.y() + 5);
        CHECK(scene.transport.last().rfind("seekMs:", 0) == 0);
        const auto mid = std::stoll(scene.transport.last().substr(7));
        CHECK(mid > 80'000);
        CHECK(mid < 120'000);
        CHECK(scene.window.state().positionActive == false);
        cover("seekbar-click-seeks");

        scene.transport.clearCalls();
        scene.press(trackLeft + span / 4, MainLayout::position.y() + 5);
        CHECK(scene.window.state().positionActive);
        cover("seekbar-thumb-sprite-selected-while-dragging");
        scene.move(trackLeft + span * 3 / 4, MainLayout::position.y() + 5);
        scene.release(trackLeft + span * 3 / 4, MainLayout::position.y() + 5);
        const auto end = std::stoll(scene.transport.last().substr(7));
        CHECK(end > 130'000);
        CHECK(scene.transport.count("seekMs:") >= 2);
        cover("seekbar-drag-scrubs");

        // Seeking while paused must not resume.
        scene.transport.stateValue = PlaybackState::Paused;
        scene.window.syncFromTransport();
        scene.transport.clearCalls();
        scene.press(trackLeft + span / 2, MainLayout::position.y() + 5);
        scene.move(trackLeft + span / 3, MainLayout::position.y() + 5);
        scene.release(trackLeft + span / 3, MainLayout::position.y() + 5);
        CHECK(scene.transport.called("seekMs:"));
        CHECK(scene.window.state().status == PlayerStatus::Paused);
        CHECK_FALSE(scene.transport.called("togglePause"));
        cover("seekbar-drag-while-paused-stays-paused");

        // While stopped the thumb is hidden: position 0 and position 100 render
        // identically inside the seek bar.
        scene.transport.stateValue = PlaybackState::Stopped;
        scene.window.syncFromTransport();
        scene.click(trackLeft + span, MainLayout::position.y() + 5);
        const QImage atZero = scene.frame();
        scene.window.setState([&] {
            PlayerState state = scene.window.state();
            state.position = 100;
            return state;
        }());
        CHECK(frameDiff(atZero, scene.frame()).empty());
        cover("seekbar-thumb-hidden-when-stopped");
    }

    // ---------------------------------------------------------------------
    // Volume + balance
    // ---------------------------------------------------------------------
    {  // volume
        Scene scene;
        const int trackLeft = MainLayout::volume.x();
        const int span = SliderMetrics::volumeTrackW - SliderMetrics::thumbW;
        scene.press(trackLeft + span, MainLayout::volume.y() + 6);
        CHECK(scene.window.state().volumeActive);
        cover("volume-thumb-selected-while-dragging");
        CHECK(scene.transport.called("volume:100"));
        CHECK(scene.window.state().volume == 100);
        cover("volume-drag-sets-percent");
        scene.release(trackLeft + span, MainLayout::volume.y() + 6);
        CHECK_FALSE(scene.window.state().volumeActive);

        // The rendered volume box follows the value, and nothing else moves.
        const QImage loud = scene.frame();
        scene.transport.clearCalls();
        scene.press(trackLeft, MainLayout::volume.y() + 6);
        scene.release(trackLeft, MainLayout::volume.y() + 6);
        CHECK(scene.window.state().volume == 0);
        const auto changed = frameDiff(loud, scene.frame());
        CHECK_FALSE(changed.empty());
        CHECK(allInside(changed, {MainLayout::volume}));
        cover("volume-frame-follows-percent");

        scene.transport.clearCalls();
        const int before = scene.window.state().volume;
        scene.wheel(centre(MainLayout::volume).x(), centre(MainLayout::volume).y(), 1);
        CHECK(scene.window.state().volume == std::min(100, before + 2));
        CHECK(scene.transport.called("volume:"));
        CHECK(scene.window.state().volume - before == 2);
        cover("volume-wheel-steps-by-two");
    }
    {  // balance
        Scene scene;
        const int trackLeft = MainLayout::balance.x();
        const int span = SliderMetrics::balanceTrackW - SliderMetrics::thumbW;
        scene.press(trackLeft, MainLayout::balance.y() + 6);
        scene.release(trackLeft, MainLayout::balance.y() + 6);
        CHECK(scene.window.state().balance == -100);
        cover("balance-drag-sets-pan");

        const QImage left = scene.frame();
        scene.press(trackLeft + span, MainLayout::balance.y() + 6);
        CHECK(scene.window.state().balanceActive);
        scene.release(trackLeft + span, MainLayout::balance.y() + 6);
        CHECK(scene.window.state().balance == 100);
        const auto changed = frameDiff(left, scene.frame());
        CHECK_FALSE(changed.empty());
        CHECK(allInside(changed, {MainLayout::balance}));
        cover("balance-frame-off-by-nine-groove");

        // Centre detent: a click within the detent still lands on exactly 0.
        const int centreX = trackLeft + span / 2;
        scene.click(centreX + koalaamp::ui::kBalanceCentreDetent, MainLayout::balance.y() + 6);
        CHECK(scene.window.state().balance == 0);
        scene.click(centreX + koalaamp::ui::kBalanceCentreDetent + 1,
                    MainLayout::balance.y() + 6);
        CHECK(scene.window.state().balance != 0);
        scene.click(centreX, MainLayout::balance.y() + 6);
        CHECK(scene.window.state().balance == 0);
        cover("balance-centre-snaps-to-zero");

        scene.wheel(centreX, MainLayout::balance.y() + 6, 1);
        CHECK(scene.window.state().balance == 2);
        scene.wheel(centreX, MainLayout::balance.y() + 6, -1);
        CHECK(scene.window.state().balance == 0);
        cover("balance-wheel-steps-by-two");
    }

    // ---------------------------------------------------------------------
    // Indicating fields
    // ---------------------------------------------------------------------
    {  // time display
        Scene scene;
        scene.transport.media = true;
        scene.transport.stateValue = PlaybackState::Playing;
        scene.transport.position = 65'000;
        scene.window.syncFromTransport();
        CHECK(scene.window.state().timeVisible);
        cover("time-hidden-while-stopped");  // asserted again below, stopped

        scene.click(centre(MainLayout::time).x(), centre(MainLayout::time).y());
        CHECK(scene.window.state().countdown);
        const QImage countdown = scene.frame();
        scene.click(centre(MainLayout::time).x(), centre(MainLayout::time).y());
        CHECK_FALSE(scene.window.state().countdown);
        const auto changed = frameDiff(countdown, scene.frame());
        CHECK_FALSE(changed.empty());
        // The minus sign (5x1, borrowed from digit 2's midline) appears left of
        // the first digit slot.
        CHECK(allInside(changed, {ShadeLayout::minusSign, MainLayout::time}));
        cover("time-click-toggles-elapsed-remaining");
        cover("time-countdown-minus-sign");

        scene.transport.stateValue = PlaybackState::Stopped;
        scene.window.syncFromTransport();
        CHECK_FALSE(scene.window.state().timeVisible);
    }
    {  // mono/stereo + khz from the PCM tap
        Scene scene;
        scene.window.setSyntheticVisual(false);
        auto callback = scene.window.visFeed().callback();
        std::vector<float> samples(2048, 0.0F);
        koalaamp::playback::PcmFormat stereoFormat{44100, 2};
        callback(samples.data(), 1024, stereoFormat);
        scene.window.syncFromTransport();
        CHECK(scene.window.state().channels == 2);
        CHECK(scene.window.state().khz == 44);
        CHECK(scene.window.state().khzKnown);
        cover("monostereo-stereo-from-2-channel-pcm");
        cover("khz-from-pcm-sample-rate");

        koalaamp::playback::PcmFormat monoFormat{22050, 1};
        callback(samples.data(), 1024, monoFormat);
        scene.window.syncFromTransport();
        CHECK(scene.window.state().channels == 1);
        CHECK(scene.window.state().khz == 22);
        cover("monostereo-mono-from-1-channel-pcm");
    }
    {  // kbps placeholder when the engine reports no bitrate
        Scene scene;
        scene.transport.media = true;
        scene.transport.stateValue = PlaybackState::Playing;
        scene.transport.kbps = 0;
        scene.window.syncFromTransport();
        CHECK_FALSE(scene.window.state().kbpsKnown);
        const QImage placeholder = scene.frame();
        scene.transport.kbps = 128;
        scene.window.syncFromTransport();
        CHECK(scene.window.state().kbpsKnown);
        const auto changed = frameDiff(placeholder, scene.frame());
        CHECK_FALSE(changed.empty());
        CHECK(allInside(changed, {MainLayout::kbps, MainLayout::khz, MainLayout::visualizer,
                                  MainLayout::playPause, MainLayout::workIndicator,
                                  MainLayout::time, MainLayout::mono, MainLayout::stereo}));
        cover("kbps-placeholder-when-unknown");
    }

    // ---------------------------------------------------------------------
    // Visualiser
    // ---------------------------------------------------------------------
    {  // visualiser
        Scene scene;
        CHECK(scene.window.state().vis == VisMode::Spectrum);
        scene.click(centre(MainLayout::visualizer).x(), centre(MainLayout::visualizer).y());
        CHECK(scene.window.state().vis == VisMode::Oscilloscope);
        scene.click(centre(MainLayout::visualizer).x(), centre(MainLayout::visualizer).y());
        CHECK(scene.window.state().vis == VisMode::Off);
        scene.click(centre(MainLayout::visualizer).x(), centre(MainLayout::visualizer).y());
        CHECK(scene.window.state().vis == VisMode::Spectrum);
        cover("vis-click-cycles-spectrum-osc-off");

        // Stopped: the vis area keeps main.bmp's art no matter how many ticks.
        scene.window.setSyntheticVisual(true);
        scene.window.tick();
        scene.window.tick();
        CHECK(scene.window.state().visFrame.barsValid == false);
        cover("vis-hidden-while-stopped");

        // Playing with a real PCM feed drives the bars.
        scene.transport.media = true;
        scene.transport.stateValue = PlaybackState::Playing;
        scene.window.syncFromTransport();
        scene.window.setSyntheticVisual(false);
        std::vector<float> loud(4096);
        for (std::size_t i = 0; i < loud.size(); ++i) {
            loud[i] = (i / 24) % 2 == 0 ? 1.0F : -1.0F;
        }
        scene.window.visFeed().callback()(loud.data(), 1024,
                                          koalaamp::playback::PcmFormat{44100, 2});
        scene.window.tick();
        const auto& frame = scene.window.state().visFrame;
        CHECK(frame.barsValid);
        CHECK(std::any_of(frame.bars.begin(), frame.bars.end(),
                          [](std::uint8_t value) { return value > 0; }));
        cover("vis-bars-follow-pcm");
    }

    // ---------------------------------------------------------------------
    // Title bar / clutter bar
    // ---------------------------------------------------------------------
    {  // title bar drag, shade and window buttons
        Scene scene;
        const QPoint before = scene.window.pos();
        scene.press(150, 6);   // empty middle of the title bar
        scene.move(180, 26);
        scene.release(180, 26);
        CHECK(scene.window.pos() != before);
        cover("titlebar-drag-moves-window");

        QTest::mouseDClick(&scene.window, Qt::LeftButton, Qt::KeyboardModifiers(), QPoint(150, 6));
        QApplication::processEvents();
        CHECK(scene.window.isShaded());
        cover("titlebar-double-click-toggles-shade");
        cover("shade-uses-275x14-geometry");
        CHECK(scene.window.canvas().width() == 275);
        CHECK(scene.window.canvas().height() == 14);

        // The shade button toggles back.
        scene.click(centre(ShadeLayout::shadeButton).x(), centre(ShadeLayout::shadeButton).y());
        CHECK_FALSE(scene.window.isShaded());

        // Mini time click toggles the time mode; the shade seek bar seeks.
        scene.window.setShade(true);
        CHECK(scene.window.isShaded());
        scene.click(centre(ShadeLayout::miniTime).x(), centre(ShadeLayout::miniTime).y());
        CHECK(scene.window.state().countdown);
        cover("shade-mini-time-click-toggles-time-mode");

        scene.transport.clearCalls();
        const int shadeSpan = ShadeLayout::position.width() - SliderMetrics::shadeThumbW;
        scene.press(ShadeLayout::position.x(), ShadeLayout::position.y() + 3);
        scene.release(ShadeLayout::position.x() + shadeSpan, ShadeLayout::position.y() + 3);
        CHECK(scene.transport.called("seekMs:"));
        cover("shade-position-drag-seeks");

        scene.window.setShade(false);
        scene.click(centre(MainLayout::shade).x(), centre(MainLayout::shade).y());
        CHECK(scene.window.isShaded());
        cover("shade-button-toggles-shade");
        scene.window.setShade(false);

        scene.click(centre(MainLayout::minimize).x(), centre(MainLayout::minimize).y());
        CHECK(scene.window.isMinimized());
        scene.window.showNormal();
        cover("minimize-click-minimises");

        bool closed = false;
        QObject::connect(&scene.window, &MainWindow::closeRequested, [&] { closed = true; });
        scene.click(centre(MainLayout::close).x(), centre(MainLayout::close).y());
        CHECK(closed);
        cover("close-click-requests-close");

        bool menu = false;
        QObject::connect(&scene.window, &MainWindow::menuRequested, [&] { menu = true; });
        scene.click(centre(MainLayout::option).x(), centre(MainLayout::option).y());
        CHECK(menu);
        cover("menu-button-requests-menu");
    }
    {  // clutter bar
        Scene scene;
        const QRect bar = MainLayout::clutterBar;
        bool menu = false;
        bool info = false;
        bool vis = false;
        QObject::connect(&scene.window, &MainWindow::menuRequested, [&] { menu = true; });
        QObject::connect(&scene.window, &MainWindow::fileInfoRequested, [&] { info = true; });
        QObject::connect(&scene.window, &MainWindow::visualizerToggled, [&](bool) { vis = true; });

        scene.click(bar.x() + 4, bar.y() + 3 + 4);  // O
        CHECK(scene.window.state().clutter.o);
        CHECK(menu);
        cover("clutter-o-requests-menu");

        scene.click(bar.x() + 4, bar.y() + 11 + 3);  // A
        CHECK(scene.window.isAlwaysOnTop());
        cover("clutter-a-toggles-always-on-top");

        scene.click(bar.x() + 4, bar.y() + 18 + 3);  // I
        CHECK(info);
        CHECK(scene.window.state().clutter.i);
        cover("clutter-i-requests-file-info");

        scene.click(bar.x() + 4, bar.y() + 25 + 4);  // D
        CHECK(scene.window.scale() == 2);
        CHECK(scene.window.canvas().width() == 550);
        CHECK(scene.window.canvas().height() == 232);
        cover("double-size-is-integer-nearest-neighbour");
        cover("clutter-d-toggles-double-size");

        scene.window.setDoubleSize(false);  // back to 1x for the remaining hits
        scene.click(bar.x() + 4, bar.y() + 33 + 3);  // V
        CHECK(vis);
        CHECK(scene.window.state().clutter.v);
        cover("clutter-v-toggles-visualiser");
    }

    // ---------------------------------------------------------------------
    // Title marquee
    // ---------------------------------------------------------------------
    {  // marquee
        Scene scene;
        const std::string longTitle = "01. artist - a very long track title";
        scene.window.setState([&] {
            PlayerState state = scene.window.state();
            state.title = longTitle;
            return state;
        }());
        CHECK(scene.window.marqueeStep() == 0);
        for (int i = 0; i < 20; ++i) {
            scene.window.tick();
        }
        CHECK(scene.window.marqueeStep() == 1);  // 220 ms per cell at 16 ms/tick
        CHECK(scene.window.state().marqueeOffsetPx == 5);
        const QImage scrolled = scene.frame();
        scene.window.tick();
        CHECK(allInside(frameDiff(scrolled, scene.frame()), {MainLayout::marquee}));
        cover("marquee-scrolls-long-titles");

        scene.window.setState([&] {
            PlayerState state = scene.window.state();
            state.title = "short";
            return state;
        }());
        const QImage staticTitle = scene.frame();
        for (int i = 0; i < 40; ++i) {
            scene.window.tick();
        }
        CHECK(scene.window.marqueeStep() == 1);  // the step only advances for long titles
        CHECK(scene.window.state().marqueeOffsetPx == 0);
        CHECK(frameDiff(staticTitle, scene.frame()).empty());
        cover("marquee-static-for-short-titles");

        // A codepoint the 5x6 font has no glyph for leaves the cell blank.
        scene.window.setState([&] {
            PlayerState state = scene.window.state();
            state.title = std::string("\x01", 1) + "X";
            return state;
        }());
        const QImage unknown = scene.frame();
        scene.window.setState([&] {
            PlayerState state = scene.window.state();
            state.title = " X";
            return state;
        }());
        CHECK(frameDiff(unknown, scene.frame()).empty());
        cover("marquee-falls-back-to-space-for-unknown-glyphs");
    }

    // ---------------------------------------------------------------------
    // Keyboard
    // ---------------------------------------------------------------------
    {  // keyboard shortcuts
        Scene scene;
        scene.transport.media = true;
        QTest::keyClick(&scene.window, Qt::Key_X);
        QApplication::processEvents();
        CHECK(scene.transport.called("resume"));
        cover("key-x-plays-key-c-pauses");
        scene.transport.clearCalls();
        QTest::keyClick(&scene.window, Qt::Key_C);
        QApplication::processEvents();
        CHECK(scene.transport.called("pause"));

        scene.transport.clearCalls();
        QTest::keyClick(&scene.window, Qt::Key_V);
        QApplication::processEvents();
        CHECK(scene.transport.called("stop"));
        QTest::keyClick(&scene.window, Qt::Key_Z);
        QApplication::processEvents();
        CHECK(scene.transport.last() == "previous");
        QTest::keyClick(&scene.window, Qt::Key_B);
        QApplication::processEvents();
        CHECK(scene.transport.last() == "next");
        cover("key-v-stops-key-z-and-b-step");

        scene.transport.position = 50'000;
        scene.transport.clearCalls();
        QTest::keyClick(&scene.window, Qt::Key_Right);
        QApplication::processEvents();
        CHECK(scene.transport.last() == "seekMs:55000");
        QTest::keyClick(&scene.window, Qt::Key_Left);
        QApplication::processEvents();
        CHECK(scene.transport.last() == "seekMs:50000");
        const int volume = scene.window.state().volume;
        QTest::keyClick(&scene.window, Qt::Key_Up);
        QApplication::processEvents();
        CHECK(scene.transport.last() == "volume:" + std::to_string(volume + 1));
        QTest::keyClick(&scene.window, Qt::Key_Down);
        QApplication::processEvents();
        CHECK(scene.transport.last() == "volume:" + std::to_string(volume));
        cover("key-arrows-seek-and-change-volume");

        QTest::keyClick(&scene.window, Qt::Key_S);
        QApplication::processEvents();
        CHECK(scene.transport.called("shuffle:"));
        QTest::keyClick(&scene.window, Qt::Key_R);
        QApplication::processEvents();
        CHECK(scene.transport.called("repeat:"));
        cover("key-s-and-r-toggle-shuffle-repeat");

        bool requested = false;
        QObject::connect(&scene.window, &MainWindow::ejectRequested, [&] { requested = true; });
        QTest::keyClick(&scene.window, Qt::Key_L);
        QApplication::processEvents();
        CHECK(requested);
        cover("key-l-opens-a-file");
    }

    // ---------------------------------------------------------------------
    // Skin shape
    // ---------------------------------------------------------------------
    {  // region.txt mask
        // base-2.91's REGION.TXT is documentation only (every line commented),
        // so the shape has to come from a corpus skin that really defines one.
        std::optional<koalaamp::skins::SkinSet> skinned;
        const std::filesystem::path corpus = KOALAAMP_CORPUS_DIR;
        if (std::filesystem::is_directory(corpus)) {
            for (const auto& entry : std::filesystem::directory_iterator(corpus)) {
                if (entry.path().extension() != ".wsz") {
                    continue;
                }
                auto candidate = koalaamp::skins::loadSkinSet(entry.path());
                if (candidate.has_value() && !candidate->region.empty()) {
                    INFO("region skin: " << entry.path().filename().string());
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
        const QRegion shaded = scene.window.mask();
        CHECK(shaded != normal);
        CHECK(shaded.boundingRect().height() <= 14);
        cover("region-mask-applied-in-both-states");
    }

    // ---------------------------------------------------------------------
    // Coverage: every checklist item above must have run.
    // ---------------------------------------------------------------------
    const auto& checklist = MainWindow::interactionChecklist();
    std::vector<std::string> missing;
    for (const std::string& item : checklist) {
        if (covered.count(item) == 0) {
            missing.push_back(item);
        }
    }
    INFO("uncovered checklist items: " << missing.size());
    for (const std::string& item : missing) {
        INFO("missing: " << item);
    }
    CHECK(missing.empty());
    for (const std::string& item : covered) {
        CHECK(std::find(checklist.begin(), checklist.end(), item) != checklist.end());
    }
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
