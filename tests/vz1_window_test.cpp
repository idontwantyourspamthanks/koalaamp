// SPDX-License-Identifier: MIT
//
// VZ-1 window-side tests (docs/tickets/VZ-1-visualizer.md): the GL canvas that
// hosts the visualizer engine.
//
// The canvas is never shown here, so initializeGL/paintGL do not run and nothing
// touches GL — the render path is verified the FE-14 way, under xvfb-run. What is
// asserted is the contract the window will lean on: an engine is always present (or
// honestly absent, with a reason), the PCM ring is the audio-thread end, and queuing
// a preset without a context is harmless.
#include <cstddef>
#include <vector>

#include <QApplication>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "interop/projectm/VisualizerEngine.hpp"
#include "ui/VisualizerCanvas.hpp"

namespace {

using koalaamp::interop::visualizer::engineAvailable;
using koalaamp::ui::VisualizerCanvas;

}  // namespace

TEST_CASE("VZ-1 the canvas owns an engine and the PCM ring", "[vz1][window]") {
    VisualizerCanvas canvas;
    // The factory never returns null, and availability is a build property: a build
    // without libprojectM must say so rather than pretend.
    CHECK(canvas.engine().available() == engineAvailable());

    // The ring is the app's producer end: it takes audio before any GL exists, and
    // hands it back in order, once.
    auto& ring = canvas.pcmRing();
    const std::vector<float> stereo{0.5F, -0.5F, 0.25F, -0.25F};
    ring.push(stereo.data(), 2, 2);
    std::vector<float> out(4, 0.0F);
    REQUIRE(ring.readNew(out.data(), 2) == 2);
    CHECK(out == stereo);
    CHECK(ring.readNew(out.data(), 2) == 0);
}

TEST_CASE("VZ-1 queuing a preset without a context is harmless", "[vz1][window]") {
    VisualizerCanvas canvas;
    canvas.requestPreset("/nonexistent/preset.milk");
    // Never shown, so paintGL has not run: the request is still queued and nothing
    // has touched GL. The assertion that matters is that neither this nor the
    // destructor crashes.
    CHECK(canvas.engine().presetName().empty());
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
