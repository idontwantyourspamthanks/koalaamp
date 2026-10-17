// SPDX-License-Identifier: MIT
//
// VZ-1 window-side tests (docs/tickets/VZ-1-visualizer.md task 1): the
// gen-chrome visualizer window that hosts the GL canvas.
//
// The canvas is never shown here, so initializeGL/paintGL do not run and nothing
// touches GL — the render path is verified the FE-14 way, under xvfb-run. What is
// asserted is the window contract: the gen frame (skinned or the designed
// fallback) around a canvas that gets the app's engine, Winamp-style close/drag,
// integer scaling, and an honest "unavailable" state in a build without
// libprojectM.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "interop/projectm/VisualizerEngine.hpp"
#include "skins/SkinSet.hpp"
#include "playback/IAudioEngine.hpp"
#include "ui/GenRenderer.hpp"
#include "ui/PcmFanout.hpp"
#include "ui/VisualizerCanvas.hpp"
#include "ui/VisualizerWindow.hpp"

namespace {

using koalaamp::interop::visualizer::IVisualizerEngine;
using koalaamp::interop::visualizer::createEngine;
using koalaamp::interop::visualizer::engineAvailable;
using koalaamp::playback::PcmFormat;
using koalaamp::ui::genLayout;
using koalaamp::ui::genSnapSize;
using koalaamp::ui::PcmFanout;
using koalaamp::ui::VisualizerCanvas;
using koalaamp::ui::VisualizerWindow;

/// The engine a build without libprojectM hands the window: present, honest
/// about being absent, and doing nothing.
class StubEngine : public IVisualizerEngine {
public:
    bool available() const override { return false; }
    bool create(int /*widthPx*/, int /*heightPx*/) override { return false; }
    void destroy() override {}
    void resize(int /*widthPx*/, int /*heightPx*/) override {}
    bool loadPresetFile(const std::string& /*path*/, bool /*smoothTransition*/) override {
        return false;
    }
    bool loadPresetData(const std::string& /*name*/, const std::string& /*data*/,
                        bool /*smoothTransition*/) override {
        return false;
    }
    void pushPcm(const float* /*interleavedStereo*/, std::size_t /*frames*/) override {}
    void render(std::uint32_t /*targetFbo*/) override {}
    std::string presetName() const override { return {}; }
    std::string lastError() const override { return "no libprojectM in this build"; }
};

void sendMouse(QWidget& widget, QEvent::Type type, const QPoint& pos, Qt::MouseButton button) {
    QMouseEvent event(type, QPointF(pos), QPointF(widget.mapToGlobal(pos)), button,
                      button == Qt::NoButton ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

void click(QWidget& widget, const QPoint& pos) {
    sendMouse(widget, QEvent::MouseButtonPress, pos, Qt::LeftButton);
    sendMouse(widget, QEvent::MouseButtonRelease, pos, Qt::LeftButton);
}

QImage renderToImage(QWidget& widget) {
    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    widget.render(&image);
    return image;
}

}  // namespace

TEST_CASE("VZ-1 the visualizer window is a gen window hosting the app's canvas",
          "[vz1][viswindow]") {
    // The window does not know how the engine was built — the app decides that.
    auto engine = createEngine();
    REQUIRE(engine != nullptr);
    VisualizerWindow window(*engine);

    CHECK(window.visualizerAvailable() == engineAvailable());
    // The reference gen window, whose client rect is what the canvas fills.
    CHECK(window.contentSize() == QSize(194, 109));
    CHECK(window.clientRect() == genLayout(QSize(194, 109)).client);
    CHECK(window.canvas().geometry() == window.clientRect());
    CHECK(&window.canvas().engine() == engine.get());
    // A build without libprojectM hides the canvas rather than showing a dead one.
    CHECK(window.canvas().isHidden() == !engineAvailable());

    // Frameless like every classic window, and rectangular: a gen frame carries
    // no region.txt shape, so no mask is applied.
    CHECK(window.windowFlags().testFlag(Qt::FramelessWindowHint));
    CHECK(window.mask().isEmpty());
}

TEST_CASE("VZ-1 the visualizer window wears the skin's gen frame", "[vz1][viswindow]") {
    // CI has no skins (they are fetched locally), so a missing fixture skips rather
    // than fails — the SE-2/SE-3 pattern.
    if (!std::filesystem::exists(std::filesystem::path(KOALAAMP_SE2_SKIN))) {
        SKIP("skin fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    const auto skinSet = koalaamp::skins::loadSkinSet(KOALAAMP_SE2_SKIN);
    REQUIRE(skinSet.has_value());
    REQUIRE(skinSet->hasSheet("GEN"));

    StubEngine engine;
    VisualizerWindow window(engine);
    window.setSkin(&*skinSet);
    CHECK_FALSE(window.usesFallbackChrome());

    const QImage rendered = renderToImage(window);
    const QImage* gen = skinSet->sheets.sheet("GEN");
    REQUIRE(gen != nullptr);
    // The title bar's left corner is the skin's own GEN_TOP_LEFT sprite, blitted
    // at (0,0) — the title text starts further right, so this pixel is the art.
    CHECK(rendered.pixelColor(3, 3) == gen->pixelColor(3, 3));
    // The bottom bar's right half is butted against the right edge.
    CHECK(rendered.pixelColor(rendered.width() - 1, rendered.height() - 1) ==
          gen->pixelColor(124, 70));
}

TEST_CASE("VZ-1 a skin without gen.bmp gets the designed fallback frame", "[vz1][viswindow]") {
    // A real corpus skin that omits gen.bmp — the case the fallback exists for.
    const auto corpus = std::filesystem::path(KOALAAMP_CORPUS_DIR);
    if (!std::filesystem::exists(corpus)) {
        SKIP("corpus missing — run scripts/fetch-corpus.sh");
    }
    std::optional<koalaamp::skins::SkinSet> skinSet;
    for (const auto& entry : std::filesystem::directory_iterator(corpus)) {
        if (entry.path().extension() != ".wsz") {
            continue;
        }
        auto candidate = koalaamp::skins::loadSkinSet(entry.path());
        if (candidate.has_value() && !candidate->hasSheet("GEN")) {
            skinSet = std::move(candidate);
            break;
        }
    }
    INFO("picked a corpus skin without gen.bmp");
    REQUIRE(skinSet.has_value());
    REQUIRE_FALSE(skinSet->hasSheet("GEN"));

    StubEngine engine;
    VisualizerWindow window(engine);
    window.setSkin(&*skinSet);
    CHECK(window.usesFallbackChrome());

    const QImage rendered = renderToImage(window);
    // The fallback frame is drawn, not left black: title bar, then the client.
    CHECK(rendered.pixelColor(3, 3) != QColor(0, 0, 0));
    CHECK(rendered.pixelColor(window.clientRect().center()) == QColor(0, 0, 0));
    // And the window is still a window: the close box is there.
    const QRect close = window.closeButtonRect();
    CHECK(rendered.pixelColor(close.center()) != QColor(0, 0, 0));
}

TEST_CASE("VZ-1 a build without libprojectM says so instead of failing", "[vz1][viswindow]") {
    StubEngine engine;
    VisualizerWindow window(engine);
    CHECK_FALSE(window.visualizerAvailable());
    CHECK(window.canvas().isHidden());

    const QImage rendered = renderToImage(window);
    // The client area is not empty black: the message is painted into it.
    const QRect client = window.clientRect();
    bool messageInk = false;
    for (int y = client.top(); y <= client.bottom() && !messageInk; ++y) {
        for (int x = client.left(); x <= client.right(); ++x) {
            if (rendered.pixelColor(x, y) != QColor(0, 0, 0)) {
                messageInk = true;
                break;
            }
        }
    }
    CHECK(messageInk);
}

TEST_CASE("VZ-1 the visualizer window drags, closes and scales like a Winamp window",
          "[vz1][viswindow]") {
    StubEngine engine;
    VisualizerWindow window(engine);

    int closes = 0;
    int drags = 0;
    QObject::connect(&window, &VisualizerWindow::closeRequested, [&closes] { ++closes; });
    QObject::connect(&window, &VisualizerWindow::dragFinished, [&drags] { ++drags; });

    // The title bar drags the window...
    const QPoint titleBar(60, koalaamp::ui::kGenTitleHeight / 2);
    sendMouse(window, QEvent::MouseButtonPress, titleBar, Qt::LeftButton);
    CHECK(window.isDragging());
    sendMouse(window, QEvent::MouseButtonRelease, titleBar, Qt::LeftButton);
    CHECK_FALSE(window.isDragging());
    CHECK(drags == 1);

    // ...but the close box does not: it closes, without starting a drag.
    const QRect close = window.closeButtonRect();
    sendMouse(window, QEvent::MouseButtonPress, close.center(), Qt::LeftButton);
    CHECK_FALSE(window.isDragging());
    sendMouse(window, QEvent::MouseButtonRelease, close.center(), Qt::LeftButton);
    CHECK(closes == 1);
    CHECK(drags == 1);

    // Releasing outside the box after pressing inside is not a close.
    sendMouse(window, QEvent::MouseButtonPress, close.center(), Qt::LeftButton);
    sendMouse(window, QEvent::MouseButtonRelease, QPoint(5, window.contentSize().height() - 5),
              Qt::LeftButton);
    CHECK(closes == 1);

    // Integer scaling doubles the window and the canvas with it.
    window.setDoubleSize(true);
    CHECK(window.size() == window.contentSize() * 2);
    CHECK(window.canvas().geometry().size() == window.clientRect().size() * 2);
    window.setDoubleSize(false);

    // A size request snaps to the tiling grid and never shrinks below reference.
    window.setClientSize(QSize(300, 200));
    CHECK(window.contentSize() == genSnapSize(QSize(300, 200)));
    CHECK(window.contentSize().width() >= 300);
    CHECK(window.contentSize().height() >= 200);
    window.setClientSize(QSize(10, 10));
    CHECK(window.contentSize() == QSize(194, 109));

    window.setAlwaysOnTop(true);
    CHECK(window.isAlwaysOnTop());
}

TEST_CASE("VZ-1 the canvas's callback is the visualizer's end of the PCM fan-out",
          "[vz1][window]") {
    auto engine = createEngine();
    VisualizerCanvas canvas(*engine);

    // The fan-out is how the visualizer gets post-DSP audio without becoming a
    // second engine tap; the canvas's callback fills the ring paintGL drains.
    koalaamp::ui::PcmFanout fanout;
    fanout.add(canvas.pcmCallback());

    PcmFormat format;
    format.channels = 2;
    const std::vector<float> stereo{0.5F, -0.5F, 0.25F, -0.25F, 0.125F, -0.125F};
    fanout.callback()(stereo.data(), 3, format);

    std::vector<float> out(6, 0.0F);
    REQUIRE(canvas.pcmRing().readNew(out.data(), 3) == 3);
    CHECK(out == stereo);

    // Mono is duplicated into both channels (MilkDrop is a stereo effect).
    format.channels = 1;
    const std::vector<float> mono{1.0F, 0.0F};
    fanout.callback()(mono.data(), 2, format);
    std::vector<float> monoOut(4, 0.0F);
    REQUIRE(canvas.pcmRing().readNew(monoOut.data(), 2) == 2);
    CHECK(monoOut == std::vector<float>{1.0F, 1.0F, 0.0F, 0.0F});
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
