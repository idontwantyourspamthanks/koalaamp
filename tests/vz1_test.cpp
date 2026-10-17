// SPDX-License-Identifier: MIT
//
// VZ-1 tests (docs/tickets/VZ-1-visualizer.md) — the visualizer's PCM plumbing.
//
// The engine's tap has exactly one owner (main.cpp installs the DSP chain), so
// the visualizer gets its audio the same way the spectrum does: as a second
// consumer of the DSP's output, through PcmFanout. These cases pin that fan-out
// and the stereo ring that feeds projectM, on the GUI side of the seam.
#include <cstddef>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "playback/IAudioEngine.hpp"
#include "ui/PcmFanout.hpp"
#include "ui/StereoPcmRing.hpp"

namespace {

using koalaamp::playback::PcmFormat;
using koalaamp::ui::PcmFanout;
using koalaamp::ui::StereoPcmRing;

/// Records what a consumer saw, so the fan-out's ordering is checkable.
struct Recorder {
    std::vector<std::size_t> frames;
    std::vector<int> channels;
    std::vector<float> firstSample;

    void operator()(const float* pcm, std::size_t frameCount, const PcmFormat& format) {
        frames.push_back(frameCount);
        channels.push_back(format.channels);
        firstSample.push_back(frameCount > 0 ? pcm[0] : 0.0F);
    }
};

}  // namespace

TEST_CASE("VZ-1 the fan-out feeds every consumer the same buffer", "[vz1][pcm]") {
    PcmFanout fanout;
    REQUIRE(fanout.consumerCount() == 0);

    Recorder spectrum;    // the main window's VisFeed
    Recorder visualizer;  // the visualizer's stereo ring
    fanout.add([&spectrum](const float* pcm, std::size_t n, const PcmFormat& f) {
        spectrum(pcm, n, f);
    });
    fanout.add([&visualizer](const float* pcm, std::size_t n, const PcmFormat& f) {
        visualizer(pcm, n, f);
    });
    CHECK(fanout.consumerCount() == 2);

    // Two pushes, different sizes, as the decoder would deliver them.
    const std::vector<float> block{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F};
    PcmFormat format;
    format.sampleRate = 44100;
    format.channels = 2;
    auto callback = fanout.callback();
    callback(block.data(), 3, format);
    callback(block.data(), 1, format);

    // Both saw both pushes, in order, with the format intact — and neither
    // consumed the other's samples.
    CHECK(spectrum.frames == std::vector<std::size_t>{3, 1});
    CHECK(visualizer.frames == std::vector<std::size_t>{3, 1});
    CHECK(spectrum.channels == std::vector<int>{2, 2});
    CHECK(visualizer.firstSample == std::vector<float>{1.0F, 1.0F});
}

TEST_CASE("VZ-1 the stereo ring keeps stereo, up-mixes mono and drops the oldest",
          "[vz1][ring]") {
    StereoPcmRing ring(1024);
    CHECK(ring.capacity() == 1024);

    // Stereo in, stereo out: unchanged, in order, both channels distinct.
    std::vector<float> stereo;
    for (int i = 0; i < 8; ++i) {
        stereo.push_back(static_cast<float>(i));
        stereo.push_back(static_cast<float>(100 + i));
    }
    ring.push(stereo.data(), 8, 2);

    std::vector<float> out(32, -1.0F);
    CHECK(ring.readRecent(out.data(), 16) == 8);
    for (int i = 0; i < 8; ++i) {
        INFO("frame " << i);
        CHECK(out[static_cast<std::size_t>(i) * 2] == static_cast<float>(i));
        CHECK(out[static_cast<std::size_t>(i) * 2 + 1] == static_cast<float>(100 + i));
    }

    // Mono is duplicated into both channels: MilkDrop is a stereo effect and
    // projectM is always fed two channels.
    const std::vector<float> mono{0.25F, 0.5F};
    ring.clear();
    ring.push(mono.data(), 2, 1);
    std::vector<float> out2(8, -1.0F);
    REQUIRE(ring.readRecent(out2.data(), 4) == 2);
    CHECK(out2[0] == 0.25F);
    CHECK(out2[1] == 0.25F);
    CHECK(out2[2] == 0.5F);
    CHECK(out2[3] == 0.5F);

    // The producer never blocks: overflowing keeps the newest window, and the
    // reader sees the most recent frames rather than a stall.
    ring.clear();
    std::vector<float> bulk(2048 * 2);
    for (std::size_t i = 0; i < 2048; ++i) {
        bulk[i * 2] = static_cast<float>(i);
        bulk[i * 2 + 1] = static_cast<float>(i);
    }
    ring.push(bulk.data(), 2048, 2);
    std::vector<float> newest(8 * 2, -1.0F);
    CHECK(ring.readRecent(newest.data(), 8) == 8);
    CHECK(newest[0] == static_cast<float>(2048 - 8));  // oldest of the surviving window
    CHECK(newest[14] == static_cast<float>(2047));     // the newest frame
}

TEST_CASE("VZ-1 the ring ignores junk and takes the first two channels", "[vz1][ring]") {
    StereoPcmRing ring(1024);

    // Nothing to read yet.
    std::vector<float> out(8, -1.0F);
    CHECK(ring.readRecent(out.data(), 4) == 0);
    CHECK(out[0] == -1.0F);

    // Junk in, nothing queued, and no crash: the audio thread must never be
    // tripped up by an odd callback.
    const std::vector<float> block{1.0F, 2.0F};
    ring.push(nullptr, 4, 2);
    ring.push(block.data(), 0, 2);
    ring.push(block.data(), 1, 0);
    CHECK(ring.readRecent(out.data(), 4) == 0);

    // More than two channels: the first two are the stereo pair.
    const std::vector<float> quad{0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F};
    ring.push(quad.data(), 2, 4);
    std::vector<float> quadOut(4, -1.0F);
    REQUIRE(ring.readRecent(quadOut.data(), 2) == 2);
    CHECK(quadOut[0] == 0.1F);
    CHECK(quadOut[1] == 0.2F);
    CHECK(quadOut[2] == 0.5F);
    CHECK(quadOut[3] == 0.6F);
}
