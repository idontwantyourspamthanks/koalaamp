// SPDX-License-Identifier: MIT
// FE-2 permanent guard for the spec §3.1 EQ rule.
//
// The load-bearing rule: libvlc's "user filters" chain — including the
// `equalizer` module that libvlc_media_player_set_equalizer installs via the
// `audio-filter` variable — runs UPSTREAM of the audio output, so the PCM
// arriving in our callback is already post-native-EQ. Therefore DSP mode must
// never call set_equalizer: doing so would double-EQ our own biquads.
//
// This test proves the rule both ways on a synthesized tone: with native EQ
// cleared the callback PCM is the baseline; installing a +12 dB native EQ
// changes it; clearing the EQ restores it. If the rule ever stopped holding
// (EQ applied downstream of our tap, or never reaching the tap), the middle
// assertion fails.
//
// Runs headless (the callback path needs no sound device) and is built/run
// wherever libvlc is found; CI installs libvlc-dev on Linux.

#include "interop/vlc/VlcPcmTap.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

void writeSineWav(const std::filesystem::path& path, double hz, unsigned rate, unsigned channels,
                  unsigned seconds) {
    std::vector<std::int16_t> pcm;
    pcm.reserve(static_cast<std::size_t>(rate) * seconds * channels);
    for (unsigned i = 0; i < rate * seconds; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(rate);
        const auto v = static_cast<std::int16_t>(
            std::lround(0.2 * 32767.0 * std::sin(2.0 * std::numbers::pi * hz * t)));
        for (unsigned c = 0; c < channels; ++c) {
            pcm.push_back(v);
        }
    }
    const auto dataBytes = static_cast<std::uint32_t>(pcm.size() * 2);
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4);
    u32(36 + dataBytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    u32(16);
    u16(1);
    u16(static_cast<std::uint16_t>(channels));
    u32(rate);
    u32(rate * channels * 2);
    u16(static_cast<std::uint16_t>(channels * 2));
    u16(16);
    f.write("data", 4);
    u32(dataBytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
}

} // namespace

TEST_CASE("libvlc hands the callback post-native-EQ PCM (spec §3.1 EQ rule)", "[vlc][eq]") {
    const std::filesystem::path wav =
        std::filesystem::temp_directory_path() / "koalaamp_fe2_eq_rule_tone.wav";
    writeSineWav(wav, 1000.0, 48000, 2, 9);
    REQUIRE(std::filesystem::exists(wav));

    koalaamp::interop::vlc::VlcPcmTap tap(1u << 20); // ring unused here; large so no drops
    if (!tap.open(wav.string())) {
        SKIP("libvlc could not open the test tone in this environment");
    }
    tap.play();

    for (int i = 0; i < 300 && tap.stats().callbacks.load() == 0; ++i) {
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(tap.stats().callbacks.load() > 0);
    REQUIRE(tap.format().sampleRate.load() == 48000);
    REQUIRE(tap.format().channels.load() == 2);

    std::this_thread::sleep_for(500ms);
    const double baseline = tap.rmsLevel();
    REQUIRE(baseline > 0.01);
    // DSP mode starts with no native EQ attached (the rule's precondition).
    CHECK_FALSE(tap.hasNativeEqualizer());

    // EQ on: +12 dB on all ten bands must raise the level the callback sees.
    tap.setEqualizerGainDb(12.0);
    std::this_thread::sleep_for(1200ms);
    const double boosted = tap.rmsLevel();
    CHECK(tap.hasNativeEqualizer());
    CHECK(boosted > baseline * 1.5);

    // EQ off: clearing the user-filter chain restores the baseline.
    tap.clearEqualizer();
    std::this_thread::sleep_for(1600ms);
    const double restored = tap.rmsLevel();
    CAPTURE(baseline, boosted, restored); // visible with ctest -V / catch -s
    CHECK_FALSE(tap.hasNativeEqualizer());
    CHECK(std::fabs(restored - baseline) < 0.15 * baseline);

    tap.stop();
    std::error_code ec;
    std::filesystem::remove(wav, ec);
}
