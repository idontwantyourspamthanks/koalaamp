// SPDX-License-Identifier: MIT
//
// VZ-1 engine-seam tests (docs/tickets/VZ-1-visualizer.md): what the visualizer
// window is allowed to assume about the engine, headlessly.
//
// The real render path needs a live GL context, so it is deliberately *not* tested
// here — these suites are registered with QT_QPA_PLATFORM=offscreen, while FE-14
// verified rendering the other way, under xvfb-run with llvmpipe. What this file
// pins is everything the window depends on that needs no context: the factory, the
// no-context failure path, and that an out-of-order driver cannot take the process
// down (which is the difference between a visualizer that fails to appear and one
// that kills the app on close).
#include <cstddef>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "interop/projectm/VisualizerEngine.hpp"

namespace {

using koalaamp::interop::visualizer::createEngine;
using koalaamp::interop::visualizer::engineAvailable;

}  // namespace

TEST_CASE("VZ-1 the engine factory always hands back an engine", "[vz1][engine]") {
    const auto engine = createEngine();
    REQUIRE(engine != nullptr);
    // Availability is a build property, and the stub reports itself off rather than
    // pretending: the window's "this build has no libprojectM" message depends on it.
    CHECK(engine->available() == engineAvailable());
}

TEST_CASE("VZ-1 create() without a GL context fails cleanly", "[vz1][engine]") {
    auto engine = createEngine();
    // This process has no OpenGL context — the situation the window must survive
    // (a session with no display, or teardown after the context has gone). It has to
    // report the failure rather than crash or half-initialise.
    CHECK_FALSE(engine->create(640, 480));
    CHECK_FALSE(engine->lastError().empty());
#ifndef KOALAAMP_HAS_PROJECTM
    // The stub says what is missing, because that string is what the window shows.
    CHECK(engine->lastError().find("libprojectM") != std::string::npos);
#endif
}

TEST_CASE("VZ-1 an engine driven out of order stays harmless", "[vz1][engine]") {
    auto engine = createEngine();
    // Each of these can plausibly arrive before create() (or after destroy()), and
    // none of them may touch GL or crash.
    engine->resize(640, 480);
    engine->pushPcm(nullptr, 0);
    const std::vector<float> pcm(64, 0.0F);
    engine->pushPcm(pcm.data(), 32);
    engine->render(0);
    CHECK(engine->presetName().empty());
    CHECK_FALSE(engine->loadPresetFile("/nonexistent.milk", false));
    CHECK_FALSE(engine->loadPresetData("inline", "per_frame_1=0;", false));

    engine->destroy();
    engine->destroy();  // idempotent
    CHECK(engine->presetName().empty());
}
