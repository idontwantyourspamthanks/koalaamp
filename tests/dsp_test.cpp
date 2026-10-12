// SPDX-License-Identifier: MIT
// AU-1 — DSP pipeline tests (docs/tickets/AU-1-dsp-eq.md).
//
// Coverage:
//   * Golden transfer functions: every band, both curve modes, plus the master
//     preamp, measured through the real DspPipeline and compared to reference
//     curves computed *independently* here from the RBJ Audio-EQ-Cookbook
//     magnitude response (method recorded: analytic references, not derived
//     from the production coefficient code; the centre-frequency gain is also
//     cross-checked against the analytic property |H(f0)| = requested dB).
//   * Winamp 0..63 slider mapping (31 == 0 dB, ±12 dB extremes).
//   * ReplayGain: album/track modes, preamp, peak-aware clip prevention,
//     −6 dB untagged fallback, R128 (LUFS → gain) — with pink-noise pairs.
//   * Limiter: the +12 dB overdrive scenario (no output past full scale).
//   * `.eqf`/`winamp.q1` byte-compatible round-trips and classic-set import.
//   * Album-keyed auto-load store + indicator hook.
//   * Coefficient crossfade is glitch-free and finite.
//   * Latency: the stage adds no buffering (≤10 ms over FE-2 passthrough).
//   * Steady-state allocation-free soak (10k blocks), the ASAN/UBSAN run.
//   * The seam installation via PlaybackCore::setPcmCallback, and the
//     no-native-EQ guard generalised to the DSP-on path.
//
// Clean-room: no WCL/Winamp source consulted. The independent reference below
// is the public RBJ cookbook; the .eqf container facts are documented in
// EqPreset.hpp with the MIT winamp-eqf reference.

#include "audio/AlbumEqStore.hpp"
#include "audio/DspPipeline.hpp"
#include "audio/EqPreset.hpp"
#include "audio/Equalizer.hpp"
#include "audio/Limiter.hpp"
#include "audio/ReplayGain.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <malloc.h>
#endif

namespace {

using namespace koalaamp::audio;
namespace fs = std::filesystem;

constexpr int kFs = 48000;
constexpr double kTolDb = 0.6;

// ---------------------------------------------------------------------------
//  independent reference: RBJ peaking magnitude response
// ---------------------------------------------------------------------------

struct RefCoeffs {
    double b0, b1, b2, a1, a2;
};

/// Written from the cookbook formula, deliberately independent of Biquad.hpp.
RefCoeffs referencePeaking(double fs, double f0, double q, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * std::numbers::pi * f0 / fs;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double cw = std::cos(w0);
    const double a0 = 1.0 + alpha / A;
    return RefCoeffs{(1.0 + alpha * A) / a0, -2.0 * cw / a0, (1.0 - alpha * A) / a0, -2.0 * cw / a0,
                     (1.0 - alpha / A) / a0};
}

double referenceMagDb(const RefCoeffs& c, double fs, double f) {
    const std::complex<double> z =
        std::exp(std::complex<double>(0.0, -2.0 * std::numbers::pi * f / fs));
    const std::complex<double> num = c.b0 + c.b1 * z + c.b2 * z * z;
    const std::complex<double> den = 1.0 + c.a1 * z + c.a2 * z * z;
    return 20.0 * std::log10(std::abs(num / den));
}

// ---------------------------------------------------------------------------
//  measurement through the real pipeline
// ---------------------------------------------------------------------------

/// Steady-state sine response (dB) of a fresh pipeline for `settings`.
/// Input amplitude 1.0 (mono), so the returned value is the gain in dB.
double measureResponseDb(const DspSettings& settings, double freq) {
    DspPipeline dsp;
    REQUIRE(dsp.prepare(kFs, 1));
    dsp.setSettings(settings);

    constexpr int kBlock = 1024;
    const long total = kFs * 2; // 2 s
    const long warm = kFs / 2;  // settle past the crossfade
    std::vector<float> buf(kBlock);
    const double w = 2.0 * std::numbers::pi * freq / kFs;
    double sum = 0.0;
    long count = 0;
    for (long off = 0; off < total; off += kBlock) {
        for (int i = 0; i < kBlock; ++i) {
            buf[i] = static_cast<float>(std::sin(w * static_cast<double>(off + i)));
        }
        dsp.process(buf.data(), kBlock, kFs, 1);
        for (int i = 0; i < kBlock; ++i) {
            if (off + i >= warm) {
                sum += static_cast<double>(buf[i]) * buf[i];
                ++count;
            }
        }
    }
    const double rms = std::sqrt(sum / static_cast<double>(count));
    return 20.0 * std::log10(rms * std::sqrt(2.0));
}

DspSettings eqOnly(EqMode mode) {
    DspSettings s;
    s.enabled = true;
    s.eq.enabled = true;
    s.eq.mode = mode;
    s.eq.preamp = kEqValueNeutral;
    s.limiterEnabled = false;
    return s;
}

std::array<double, 15> testFrequencies() {
    return {20.0,   40.0,   60.0,   90.0,   170.0,   310.0,   600.0,  1000.0,
            1500.0, 3000.0, 6000.0, 9000.0, 12000.0, 16000.0, 19000.0};
}

// ---------------------------------------------------------------------------
//  pink noise (Paul Kellet's economy filter), deterministic
// ---------------------------------------------------------------------------

std::vector<float> pinkNoise(std::size_t frames, std::uint32_t seed = 0x1234abcd) {
    std::minstd_rand rng(seed);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double white = dist(rng);
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
        b6 = white * 0.115926;
        out[i] = static_cast<float>(pink * 0.11);
    }
    return out;
}

double rms(const std::vector<float>& v) {
    double sum = 0.0;
    for (float x : v) {
        sum += static_cast<double>(x) * x;
    }
    return std::sqrt(sum / static_cast<double>(v.size()));
}

double peak(const std::vector<float>& v) {
    double p = 0.0;
    for (float x : v) {
        p = std::max(p, std::abs(static_cast<double>(x)));
    }
    return p;
}

std::size_t heapInUseBytes() {
#if defined(__linux__) && defined(__GLIBC__) &&                                                    \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    const struct mallinfo2 info = mallinfo2();
    return static_cast<std::size_t>(info.uordblks);
#else
    return 0;
#endif
}

} // namespace

// ===========================================================================
//  golden transfer functions
// ===========================================================================

TEST_CASE("EQ: every band matches the analytic peaking reference (both modes)",
          "[au1][dsp][eq][golden]") {
    const auto freqs = testFrequencies();
    for (EqMode mode : {EqMode::WinampCurve, EqMode::ConstantQ}) {
        for (int band = 0; band < kEqBands; ++band) {
            for (double gainDb : {12.0, -12.0}) {
                DspSettings s = eqOnly(mode);
                s.eq.bands[static_cast<std::size_t>(band)] = eqDbToValue(gainDb);
                const double q = bandQ(mode, gainDb);
                const RefCoeffs ref = referencePeaking(
                    kFs, kEqFrequencies[static_cast<std::size_t>(band)], q, gainDb);
                for (double f : freqs) {
                    const double measured = measureResponseDb(s, f);
                    const double expected = referenceMagDb(ref, kFs, f);
                    INFO("mode=" << (mode == EqMode::ConstantQ ? "constant-Q" : "winamp")
                                 << " band=" << kEqFrequencies[static_cast<std::size_t>(band)]
                                 << "Hz gain=" << gainDb << "dB f=" << f << "Hz");
                    CHECK(measured == Catch::Approx(expected).margin(kTolDb));
                }
            }
        }
    }
}

TEST_CASE("EQ: centre-frequency gain equals the requested gain (analytic property)",
          "[au1][dsp][eq][golden]") {
    for (EqMode mode : {EqMode::WinampCurve, EqMode::ConstantQ}) {
        for (int band = 0; band < kEqBands; ++band) {
            DspSettings s = eqOnly(mode);
            s.eq.bands[static_cast<std::size_t>(band)] = eqDbToValue(12.0);
            const double measured =
                measureResponseDb(s, kEqFrequencies[static_cast<std::size_t>(band)]);
            INFO("mode=" << static_cast<int>(mode) << " band=" << band);
            CHECK(measured == Catch::Approx(12.0).margin(0.25));
        }
    }
}

TEST_CASE("EQ: Winamp curve and Constant-Q differ off-centre", "[au1][dsp][eq][golden]") {
    int differences = 0;
    for (int band = 0; band < kEqBands; ++band) {
        DspSettings w = eqOnly(EqMode::WinampCurve);
        DspSettings c = eqOnly(EqMode::ConstantQ);
        w.eq.bands[static_cast<std::size_t>(band)] = eqDbToValue(12.0);
        c.eq.bands[static_cast<std::size_t>(band)] = eqDbToValue(12.0);
        const double f = kEqFrequencies[static_cast<std::size_t>(band)] * 2.0;
        if (f >= kFs / 2.0) {
            continue;
        }
        const double dw = measureResponseDb(w, f);
        const double dc = measureResponseDb(c, f);
        if (std::abs(dw - dc) > 0.5) {
            ++differences;
        }
    }
    CHECK(differences > 0); // proportional-Q (narrower at +12) vs constant-Q
}

TEST_CASE("EQ: full preset matches the product of per-band references", "[au1][dsp][eq][golden]") {
    const EqPreset rock = classicEqPresets()[8]; // "Rock"
    REQUIRE(rock.name == "Rock");
    for (EqMode mode : {EqMode::WinampCurve, EqMode::ConstantQ}) {
        DspSettings s = eqOnly(mode);
        s.eq.bands = rock.bands;
        for (double f : testFrequencies()) {
            double expected = 0.0;
            for (int b = 0; b < kEqBands; ++b) {
                const double g = eqValueToDb(rock.bands[static_cast<std::size_t>(b)]);
                expected += referenceMagDb(
                    referencePeaking(kFs, kEqFrequencies[static_cast<std::size_t>(b)],
                                     bandQ(mode, g), g),
                    kFs, f);
            }
            CHECK(measureResponseDb(s, f) == Catch::Approx(expected).margin(kTolDb));
        }
    }
}

TEST_CASE("EQ: master preamp is a flat gain", "[au1][dsp][eq][golden]") {
    DspSettings s = eqOnly(EqMode::WinampCurve);
    s.eq.preamp = eqDbToValue(12.0); // +12 dB
    for (double f : {100.0, 1000.0, 5000.0, 12000.0}) {
        CHECK(measureResponseDb(s, f) == Catch::Approx(12.0).margin(0.1));
    }
}

TEST_CASE("EQ: slider mapping (31 == 0 dB, 0 == +12, 63 == -12)", "[au1][dsp][eq]") {
    CHECK(eqValueToDb(kEqValueNeutral) == Catch::Approx(0.0));
    CHECK(eqValueToDb(kEqValueMin) == Catch::Approx(12.0));
    CHECK(eqValueToDb(kEqValueMax) == Catch::Approx(-12.0));
    CHECK(eqDbToValue(12.0) == kEqValueMin);
    CHECK(eqDbToValue(-12.0) == kEqValueMax);
    CHECK(eqDbToValue(0.0) == kEqValueNeutral);
    for (int v = kEqValueMin; v <= kEqValueMax; ++v) {
        CHECK(eqDbToValue(eqValueToDb(v)) == v);
    }
}

// ===========================================================================
//  ReplayGain
// ===========================================================================

TEST_CASE("ReplayGain: modes, fallback, preamp and R128", "[au1][dsp][rg]") {
    RgInfo info;
    info.trackGainDb = -7.0;
    info.albumGainDb = -5.0;

    RgConfig cfg;
    cfg.mode = RgMode::Off;
    CHECK(computeReplayGain(info, cfg).linearGain == Catch::Approx(1.0));

    cfg.mode = RgMode::Track;
    CHECK(computeReplayGain(info, cfg).gainDb == Catch::Approx(-7.0));

    cfg.mode = RgMode::Album;
    CHECK(computeReplayGain(info, cfg).gainDb == Catch::Approx(-5.0));

    cfg.preampDb = 3.0;
    CHECK(computeReplayGain(info, cfg).gainDb == Catch::Approx(-2.0));
    cfg.preampDb = 0.0;

    // Album falls back to track when no album tag exists.
    RgInfo noAlbum;
    noAlbum.trackGainDb = -4.0;
    CHECK(computeReplayGain(noAlbum, cfg).gainDb == Catch::Approx(-4.0));

    // Untagged → −6 dB fallback.
    RgInfo empty;
    const RgResult fb = computeReplayGain(empty, cfg);
    CHECK(fb.fallback);
    CHECK(fb.gainDb == Catch::Approx(-6.0));

    // R128: gain = −18 − LUFS.
    RgInfo r128;
    r128.trackLufs = -23.0;
    cfg.mode = RgMode::Track;
    CHECK(computeReplayGain(r128, cfg).gainDb == Catch::Approx(5.0));
    CHECK(gainFromLufs(-18.0) == Catch::Approx(0.0));
    CHECK(gainFromR128Q78(-1280) == Catch::Approx(-5.0));

    CHECK(parseGainDb("-9.50 dB").value() == Catch::Approx(-9.5));
    CHECK(parseGainDb("+3.2dB").value() == Catch::Approx(3.2));
    CHECK_FALSE(parseGainDb("loud").has_value());
    CHECK(parseLufs("-23.0 LUFS").value() == Catch::Approx(-23.0));
}

TEST_CASE("ReplayGain: peak-aware clip prevention", "[au1][dsp][rg]") {
    RgInfo info;
    info.trackGainDb = 6.0;
    info.trackPeak = 0.9;
    RgConfig cfg;
    cfg.mode = RgMode::Track;
    cfg.preventClip = true;
    const RgResult r = computeReplayGain(info, cfg);
    CHECK(r.clipLimited);
    CHECK(r.linearGain == Catch::Approx(1.0 / 0.9).epsilon(1e-6));

    cfg.preventClip = false;
    CHECK_FALSE(computeReplayGain(info, cfg).clipLimited);
    CHECK(computeReplayGain(info, cfg).gainDb == Catch::Approx(6.0));
}

TEST_CASE("ReplayGain: pink-noise pairs measure the applied gain", "[au1][dsp][rg]") {
    const auto pink = pinkNoise(48000); // 1 s mono
    const double base = rms(pink);

    struct Pair {
        RgMode mode;
        RgInfo info;
        double expectDb;
    };
    std::vector<Pair> pairs = {
        {RgMode::Track,
         RgInfo{std::optional<double>(-3.0), std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::nullopt},
         -3.0},
        {RgMode::Album,
         RgInfo{std::optional<double>(-3.0), std::nullopt, std::optional<double>(2.0), std::nullopt,
                std::nullopt, std::nullopt},
         2.0},
        {RgMode::Track,
         RgInfo{std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                std::optional<double>(-20.0), std::nullopt},
         2.0},                           // R128: -18-(-20)
        {RgMode::Track, RgInfo{}, -6.0}, // fallback
    };

    for (const Pair& p : pairs) {
        DspPipeline dsp;
        REQUIRE(dsp.prepare(kFs, 1));
        DspSettings s;
        s.enabled = true;
        s.rg.mode = p.mode;
        s.rg.preventClip = false;
        dsp.setSettings(s);
        dsp.setReplayGain(p.info);

        std::vector<float> buf = pink;
        constexpr int kBlock = 1024;
        for (std::size_t off = 0; off < buf.size(); off += kBlock) {
            const int n = static_cast<int>(std::min<std::size_t>(kBlock, buf.size() - off));
            dsp.process(buf.data() + off, static_cast<std::size_t>(n), kFs, 1);
        }
        const double gotDb = 20.0 * std::log10(rms(buf) / base);
        INFO("mode=" << static_cast<int>(p.mode) << " expect=" << p.expectDb);
        CHECK(gotDb == Catch::Approx(p.expectDb).margin(0.05));
    }
}

// ===========================================================================
//  limiter
// ===========================================================================

TEST_CASE("Limiter: +12 dB overdrive never passes full scale", "[au1][dsp][limiter]") {
    const auto pink = pinkNoise(48000);

    auto run = [&](bool limiter) {
        DspPipeline dsp;
        REQUIRE(dsp.prepare(kFs, 1));
        DspSettings s;
        s.enabled = true;
        s.limiterEnabled = limiter;
        s.rg.mode = RgMode::Track;
        s.rg.preventClip = false;
        dsp.setSettings(s);
        RgInfo info;
        info.trackGainDb = 12.0; // +12 dB overdrive
        dsp.setReplayGain(info);
        std::vector<float> buf = pink;
        constexpr int kBlock = 1024;
        for (std::size_t off = 0; off < buf.size(); off += kBlock) {
            const int n = static_cast<int>(std::min<std::size_t>(kBlock, buf.size() - off));
            dsp.process(buf.data() + off, static_cast<std::size_t>(n), kFs, 1);
        }
        return buf;
    };

    const std::vector<float> limited = run(true);
    const std::vector<float> unlimited = run(false);
    CHECK(peak(unlimited) > 1.0);       // without the limiter the boost clips
    CHECK(peak(limited) <= 1.0 + 1e-4); // the limiter holds full scale
    CHECK(rms(limited) < rms(unlimited));

    Limiter lim;
    lim.setEnabled(true);
    CHECK(lim.process(0.5f) == Catch::Approx(0.5f).margin(1e-6)); // below the knee
}

// ===========================================================================
//  .eqf / winamp.q1
// ===========================================================================

TEST_CASE("EQ preset: .eqf byte layout is Winamp-compatible", "[au1][dsp][eqf]") {
    EqPreset p;
    p.name = "Entry1";
    p.bands = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // Winamp value 0 == +12 dB (max boost)
    p.preamp = 31;
    auto bytes = writeEqf(p);
    REQUIRE(bytes.size() == kEqfPresetBytes); // 299
    CHECK(bytes.size() == 299);

    const std::string magic(reinterpret_cast<const char*>(bytes.data()), 27);
    CHECK(magic == "Winamp EQ library file v1.1");
    CHECK(bytes[27] == 0x1A);
    CHECK(bytes[28] == '!');
    CHECK(bytes[29] == '-');
    CHECK(bytes[30] == '-');
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data() + 31), 6) == "Entry1");
    CHECK(bytes[31 + 6] == 0); // name NUL-terminated
    CHECK(bytes[298] == 31);   // preamp byte last
    for (int b = 0; b < 10; ++b) {
        CHECK(bytes[288 + b] == 0);
    }
}

TEST_CASE("EQ preset: round-trip is byte-identical on a fixture set", "[au1][dsp][eqf]") {
    std::vector<EqPreset> fixtures;
    fixtures.push_back(EqPreset{}); // empty name, neutral
    EqPreset max;
    max.name = "Max";
    max.bands = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    max.preamp = 0;
    fixtures.push_back(max);
    EqPreset min;
    min.name = "Min";
    min.bands = {63, 63, 63, 63, 63, 63, 63, 63, 63, 63};
    min.preamp = 63;
    fixtures.push_back(min);
    EqPreset longName;
    longName.name = std::string(300, 'x'); // longer than the 257-byte field
    fixtures.push_back(longName);

    const std::vector<std::uint8_t> bytes = writeEqfLibrary(fixtures);
    CHECK(bytes.size() == kEqfHeaderBytes + fixtures.size() * (kEqfNameBytes + kEqfValuesBytes));

    const std::vector<EqPreset> parsed = parseEqf(bytes);
    REQUIRE(parsed.size() == fixtures.size());
    for (std::size_t i = 0; i < fixtures.size(); ++i) {
        CHECK(parsed[i].bands == fixtures[i].bands);
        CHECK(parsed[i].preamp == fixtures[i].preamp);
        CHECK(parsed[i].name.size() <= kEqfNameBytes - 1);
    }
    // A second write of the parsed data reproduces the bytes exactly.
    CHECK(writeEqfLibrary(parsed) == bytes);
}

TEST_CASE("EQ preset: winamp.q1 imports the classic preset set", "[au1][dsp][eqf]") {
    const std::vector<EqPreset>& classic = classicEqPresets();
    REQUIRE(classic.size() == 17);

    const std::vector<std::uint8_t> q1 = writeEqfLibrary(classic);
    const std::vector<EqPreset> imported = importWinampQ1(q1.data(), q1.size());
    REQUIRE(imported.size() == classic.size());
    for (std::size_t i = 0; i < classic.size(); ++i) {
        CHECK(imported[i].name == classic[i].name);
        CHECK(imported[i].bands == classic[i].bands);
        CHECK(imported[i].preamp == classic[i].preamp);
    }
    // A published Winamp.q1 byte sequence parses to the documented values.
    CHECK(classic[0].name == "Classical");
    CHECK(classic[0].bands == std::array<int, 10>{31, 31, 31, 31, 31, 31, 44, 44, 44, 48});
    CHECK(classic[13].name == "Full Treble");
    CHECK(classic[13].bands == std::array<int, 10>{48, 48, 48, 39, 27, 14, 6, 6, 6, 4});
}

TEST_CASE("EQ preset: rejects foreign data", "[au1][dsp][eqf]") {
    const std::vector<std::uint8_t> junk(300, 0x41);
    CHECK(parseEqf(junk).empty());
    CHECK_FALSE(isEqfLibrary(junk.data(), junk.size()));
    CHECK(parseEqf(nullptr, 0).empty());
}

TEST_CASE("EQ preset: committed fixtures round-trip byte-identically", "[au1][dsp][eqf]") {
    const fs::path dir = KOALAAMP_EQ_FIXTURE_DIR;
    REQUIRE(fs::exists(dir));

    // Fixtures are generated by tests/fixtures/eq/gen_fixtures.py, independently
    // of the C++ codec, from the documented Winamp layout.
    const std::array<std::pair<const char*, std::size_t>, 6> cases{{
        {"classical.eqf", 1},
        {"full_treble.eqf", 1},
        {"flat.eqf", 1},
        {"max.eqf", 1},
        {"min.eqf", 1},
        {"winamp.q1", 17},
    }};
    for (const auto& [name, count] : cases) {
        std::ifstream in(dir / name, std::ios::binary);
        REQUIRE(in.good());
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                              std::istreambuf_iterator<char>());
        REQUIRE(isEqfLibrary(bytes.data(), bytes.size()));
        const std::vector<EqPreset> parsed = parseEqf(bytes);
        INFO("fixture=" << name);
        REQUIRE(parsed.size() == count);
        // Re-serialise and require the exact same bytes Winamp would read.
        CHECK(writeEqfLibrary(parsed) == bytes);
    }

    auto load = [&](const char* name) {
        std::ifstream in(dir / name, std::ios::binary);
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
    };
    const auto flat = parseEqf(load("flat.eqf"));
    REQUIRE(flat.size() == 1);
    CHECK(flat[0].bands == std::array<int, 10>{31, 31, 31, 31, 31, 31, 31, 31, 31, 31});
    const auto maxB = parseEqf(load("max.eqf"));
    CHECK(maxB[0].bands == std::array<int, 10>{0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    const auto minB = parseEqf(load("min.eqf"));
    CHECK(minB[0].bands == std::array<int, 10>{63, 63, 63, 63, 63, 63, 63, 63, 63, 63});
    const auto treble = parseEqf(load("full_treble.eqf"));
    CHECK(treble[0].bands == std::array<int, 10>{48, 48, 48, 39, 27, 14, 6, 6, 6, 4});
    CHECK(parseEqf(load("winamp.q1")) == classicEqPresets());
}

// ===========================================================================
//  auto-load store
// ===========================================================================

TEST_CASE("AlbumEqStore: album-keyed auto-load + indicator hook", "[au1][dsp][eq]") {
    AlbumEqStore store;
    int indicatorCalls = 0;
    bool lastActive = false;
    std::string lastApplied;
    store.setIndicatorHook([&](bool active, const EqPreset& p) {
        ++indicatorCalls;
        lastActive = active;
        lastApplied = p.name;
    });

    store.setPreset("Album A", classicEqPresets()[8]); // Rock
    store.setEnabled(true);

    const auto applied = store.onAlbumChanged("Album A");
    REQUIRE(applied.has_value());
    CHECK(applied->name == "Rock");
    CHECK(indicatorCalls == 1);
    CHECK(lastActive);
    CHECK(lastApplied == "Rock");

    // Unknown album: no preset, indicator cleared.
    store.onAlbumChanged("Album B");
    CHECK(indicatorCalls == 2);
    CHECK_FALSE(lastActive);
    CHECK_FALSE(store.preset("Album B").has_value());

    // Auto-load off: no preset applied even when present.
    store.setEnabled(false);
    CHECK_FALSE(store.onAlbumChanged("Album A").has_value());
}

// ===========================================================================
//  crossfade + latency + soak
// ===========================================================================

TEST_CASE("EQ: coefficient crossfade is finite and glitch-free", "[au1][dsp][eq]") {
    DspPipeline dsp;
    REQUIRE(dsp.prepare(kFs, 1));
    DspSettings s = eqOnly(EqMode::WinampCurve);
    dsp.setSettings(s);

    constexpr int kBlock = 1024;
    std::vector<float> buf(kBlock);
    double last = 0.0;
    double maxJump = 0.0;
    const double w = 2.0 * std::numbers::pi * 1000.0 / kFs;
    long n = 0;
    // Jump the band gain around on every block; the output must stay bounded.
    for (int change = 0; change < 40; ++change) {
        s.eq.bands[4] = (change % 2 == 0) ? 0 : 63; // ±12 dB on 1 kHz
        dsp.setSettings(s);
        for (int i = 0; i < kBlock; ++i) {
            buf[i] = static_cast<float>(std::sin(w * static_cast<double>(n++)));
        }
        dsp.process(buf.data(), kBlock, kFs, 1);
        for (float x : buf) {
            REQUIRE(std::isfinite(x));
            CHECK(std::abs(x) < 10.0f);
            maxJump = std::max(maxJump, std::abs(static_cast<double>(x) - last));
            last = x;
        }
    }
    CHECK(maxJump < 0.5); // stepwise sine, no impulsive discontinuity
}

TEST_CASE("DSP pipeline adds no buffering latency (<=10 ms over passthrough)",
          "[au1][dsp][latency]") {
    CHECK(DspPipeline::addedLatencyFrames() == 0);

    DspPipeline dsp;
    REQUIRE(dsp.prepare(kFs, 1));
    DspSettings s = eqOnly(EqMode::ConstantQ);
    s.eq.bands[0] = 0; // +12 dB @ 60 Hz so b0 != 0 and there is a real response
    dsp.setSettings(s);

    std::vector<float> buf(1024, 0.0f);
    buf[0] = 1.0f; // impulse
    dsp.process(buf.data(), buf.size(), kFs, 1);
    // A peaking biquad's direct-form output starts at the same sample index:
    // the pipeline introduces no delay line.
    CHECK(std::abs(buf[0]) > 1e-6f);
    CHECK(DspPipeline::addedLatencyFrames() * 1000 / kFs <= 10);
}

TEST_CASE("DSP pipeline: steady-state is allocation-free (10k-block soak)", "[au1][dsp][soak]") {
    DspPipeline dsp;
    REQUIRE(dsp.prepare(kFs, 2));
    DspSettings s;
    s.enabled = true;
    s.eq.enabled = true;
    s.eq.mode = EqMode::WinampCurve;
    s.eq.bands = classicEqPresets()[8].bands; // Rock, 10 active bands
    s.eq.preamp = 0;                          // +12 dB master preamp
    s.limiterEnabled = true;
    s.rg.mode = RgMode::Album;
    s.rg.preventClip = false;
    dsp.setSettings(s);
    RgInfo rg;
    rg.albumGainDb = 3.0;
    dsp.setReplayGain(rg);

    constexpr int kBlock = 1024;
    std::vector<float> buf(static_cast<std::size_t>(kBlock) * 2);
    const double w = 2.0 * std::numbers::pi * 440.0 / kFs;
    auto fill = [&](long n) {
        for (int i = 0; i < kBlock; ++i) {
            const float v = static_cast<float>(0.3 * std::sin(w * static_cast<double>(n + i)));
            buf[static_cast<std::size_t>(i) * 2] = v;
            buf[static_cast<std::size_t>(i) * 2 + 1] = v;
        }
    };

    // Warm up (first latch, buffers already allocated by prepare).
    fill(0);
    dsp.process(buf.data(), kBlock, kFs, 2);

    const std::size_t before = heapInUseBytes();
    constexpr int kBlocks = 10000;
    for (int i = 0; i < kBlocks; ++i) {
        fill(static_cast<long>(i) * kBlock);
        dsp.process(buf.data(), kBlock, kFs, 2);
    }
    const std::size_t after = heapInUseBytes();

    CHECK(dsp.stats().processedBlocks == kBlocks + 1);
    if (before != 0) {
        CHECK(before == after); // zero heap growth across 10k blocks
    }
}

// ===========================================================================
//  seam installation + no-native-EQ guard (generalised to the DSP-on path)
// ===========================================================================

TEST_CASE("src/audio never touches libvlc native EQ or Qt (spec 2.2/3.1)", "[au1][dsp][guard]") {
    const fs::path dir = KOALAAMP_AUDIO_SRC;
    REQUIRE(fs::exists(dir));
    std::vector<std::string> offenders;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string ext = entry.path().extension().string();
        if (ext != ".hpp" && ext != ".cpp") {
            continue;
        }
        std::ifstream stream(entry.path());
        std::string line;
        int number = 0;
        while (std::getline(stream, line)) {
            ++number;
            const bool qtInclude = line.find("#include <Q") != std::string::npos ||
                                   line.find("#include \"Q") != std::string::npos;
            const bool vlcHeader = line.find("vlc/vlc.h") != std::string::npos;
            const bool nativeEq =
                line.find("libvlc_media_player_set_equalizer") != std::string::npos ||
                line.find("audio-filter=") != std::string::npos;
            if (qtInclude || vlcHeader || nativeEq) {
                offenders.push_back(entry.path().string() + ":" + std::to_string(number) + ": " +
                                    line);
            }
        }
    }
    for (const std::string& o : offenders) {
        INFO("offending line: " << o);
    }
    CHECK(offenders.empty());
}

#if defined(KOALAAMP_HAS_PLAYBACK_CORE)
#include "playback/PlaybackCore.hpp"

namespace {
void writeSineWav(const fs::path& path, double hz, unsigned rate, double seconds) {
    std::vector<std::int16_t> pcm;
    const auto frames = static_cast<unsigned>(rate * seconds);
    for (unsigned i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / rate;
        pcm.push_back(static_cast<std::int16_t>(
            std::lround(0.5 * 32767.0 * std::sin(2.0 * std::numbers::pi * hz * t))));
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
    u16(2);
    u32(rate);
    u32(rate * 4);
    u16(4);
    u16(16);
    f.write("data", 4);
    u32(dataBytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
}
} // namespace

TEST_CASE("DSP transform installs at the PlaybackCore seam without native EQ", "[au1][dsp][seam]") {
    namespace playback = koalaamp::playback;
    const fs::path wav =
        fs::temp_directory_path() /
        ("koalaamp_au1_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
    writeSineWav(wav, 1000.0, 48000, 4.0);

    playback::PlaybackConfig config;
    config.output = playback::PlaybackConfig::Output::Null;
    // Declaration order is load-bearing (same contract as main.cpp): the DSP
    // must outlive the core — the core's amem thread invokes the pipeline's
    // callback, so destroying dsp first is a use-after-free (a ~1-in-5 SIGSEGV
    // observed here before this fix). dsp first, core last.
    DspPipeline dsp;
    DspSettings s;
    s.enabled = true;
    s.eq.enabled = true;
    s.eq.bands[4] = 0; // +12 dB @ 1 kHz, so the stage does real work
    dsp.setSettings(s);
    playback::PlaybackCore core(config);
    std::atomic<std::size_t> downstreamFrames{0};
    std::atomic<bool> sawPcm{false};
    auto downstream = [&](const float*, std::size_t frames, const playback::PcmFormat&) {
        downstreamFrames.fetch_add(frames);
        sawPcm.store(true);
    };
    core.setPcmCallback(dsp.makeCallback(downstream));
    core.play(playback::MediaRef{playback::MediaRef::Kind::Path, wav.string()});

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline && dsp.stats().processedBlocks == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CHECK(sawPcm.load());
    CHECK(dsp.stats().processedBlocks > 0);
    CHECK(dsp.stats().frames > 0);
    CHECK_FALSE(core.metrics().nativeEqEnabled); // spec §3.1 EQ rule, DSP-on path
    core.stop();
    std::error_code ec;
    fs::remove(wav, ec);
}
#endif

// ===========================================================================
//  AU-2 — clean-room ReplayGain/R128 tag reader (RgTags)
// ===========================================================================
#include "audio/RgTags.hpp"

namespace {

using ByteVec = std::vector<std::uint8_t>;

void pushBytes(ByteVec& b, std::string_view s) {
    b.insert(b.end(), s.begin(), s.end());
}

void pushU32LE(ByteVec& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

void pushU32BE(ByteVec& b, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) {
        b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

void pushU24BE(ByteVec& b, std::uint32_t v) {
    for (int i = 2; i >= 0; --i) {
        b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

void pushU16BE(ByteVec& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void pushSyncSafe(ByteVec& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>((v >> 21) & 0x7F));
    b.push_back(static_cast<std::uint8_t>((v >> 14) & 0x7F));
    b.push_back(static_cast<std::uint8_t>((v >> 7) & 0x7F));
    b.push_back(static_cast<std::uint8_t>(v & 0x7F));
}

/// vendor_length=0, count, then len+entry for each "KEY=VALUE".
ByteVec vorbisComment(const std::vector<std::string>& entries) {
    ByteVec b;
    pushU32LE(b, 0);
    pushU32LE(b, static_cast<std::uint32_t>(entries.size()));
    for (const std::string& e : entries) {
        pushU32LE(b, static_cast<std::uint32_t>(e.size()));
        pushBytes(b, e);
    }
    return b;
}

ByteVec flacWithVorbisComment(const std::vector<std::string>& entries) {
    ByteVec b;
    pushBytes(b, "fLaC");
    const ByteVec vc = vorbisComment(entries);
    b.push_back(0x80 | 4); // last metadata block, type 4 = VORBIS_COMMENT
    pushU24BE(b, static_cast<std::uint32_t>(vc.size()));
    b.insert(b.end(), vc.begin(), vc.end());
    return b;
}

/// One Ogg page holding `packets` (each < 255 bytes -> a single lacing value);
/// the CRC field is written as 0 (the reader never validates it).
ByteVec oggPage(std::uint8_t headerType, const std::vector<ByteVec>& packets) {
    ByteVec b;
    pushBytes(b, "OggS");
    b.push_back(0);          // version
    b.push_back(headerType); // header type flags
    for (int i = 0; i < 8; ++i) {
        b.push_back(0); // granule position
    }
    pushU32LE(b, 0); // bitstream serial
    pushU32LE(b, 0); // page sequence
    pushU32LE(b, 0); // CRC
    b.push_back(static_cast<std::uint8_t>(packets.size()));
    for (const ByteVec& p : packets) {
        b.push_back(static_cast<std::uint8_t>(p.size()));
    }
    for (const ByteVec& p : packets) {
        b.insert(b.end(), p.begin(), p.end());
    }
    return b;
}

ByteVec vorbisIdPacket() {
    ByteVec p{0x01, 'v', 'o', 'r', 'b', 'i', 's'};
    p.resize(30, 0);
    return p;
}

ByteVec vorbisCommentPacket(const std::vector<std::string>& entries) {
    ByteVec p{0x03, 'v', 'o', 'r', 'b', 'i', 's'};
    const ByteVec vc = vorbisComment(entries);
    p.insert(p.end(), vc.begin(), vc.end());
    return p;
}

ByteVec opusIdPacket() {
    ByteVec p;
    pushBytes(p, "OpusHead");
    p.resize(19, 0);
    return p;
}

ByteVec opusCommentPacket(const std::vector<std::string>& entries) {
    ByteVec p;
    pushBytes(p, "OpusTags");
    const ByteVec vc = vorbisComment(entries);
    p.insert(p.end(), vc.begin(), vc.end());
    return p;
}

/// UTF-8/latin1 TXXX payload: encoding, description, 0x00, value.
ByteVec txxxPayload(std::uint8_t enc, std::string_view desc, std::string_view value) {
    ByteVec b;
    b.push_back(enc);
    pushBytes(b, desc);
    b.push_back(0x00);
    pushBytes(b, value);
    return b;
}

/// UTF-16LE TXXX payload (encoding 1, little-endian BOM).
ByteVec txxxPayloadUtf16Le(std::string_view desc, std::string_view value) {
    ByteVec b;
    b.push_back(0x01);
    b.push_back(0xFF);
    b.push_back(0xFE);
    for (char c : desc) {
        b.push_back(static_cast<std::uint8_t>(c));
        b.push_back(0x00);
    }
    b.push_back(0x00);
    b.push_back(0x00);
    for (char c : value) {
        b.push_back(static_cast<std::uint8_t>(c));
        b.push_back(0x00);
    }
    b.push_back(0x00);
    b.push_back(0x00);
    return b;
}

ByteVec id3Frame24(std::string_view id, const ByteVec& payload, std::uint8_t flagsLow = 0) {
    ByteVec b;
    pushBytes(b, id);
    pushSyncSafe(b, static_cast<std::uint32_t>(payload.size()));
    b.push_back(0x00);
    b.push_back(flagsLow);
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}

ByteVec id3Frame23(std::string_view id, const ByteVec& payload) {
    ByteVec b;
    pushBytes(b, id);
    pushU32BE(b, static_cast<std::uint32_t>(payload.size()));
    pushU16BE(b, 0);
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}

ByteVec id3File(std::uint8_t major, std::uint8_t flags, const ByteVec& frames) {
    ByteVec b;
    pushBytes(b, "ID3");
    b.push_back(major);
    b.push_back(0x00);
    b.push_back(flags);
    pushSyncSafe(b, static_cast<std::uint32_t>(frames.size()));
    b.insert(b.end(), frames.begin(), frames.end());
    return b;
}

/// ID3 unsynchronisation as a tagger applies it on disk: insert 0x00 after an
/// 0xFF whose successor is 0x00 or >= 0xE0.
ByteVec synchronise(const ByteVec& in) {
    ByteVec out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        out.push_back(in[i]);
        if (in[i] == 0xFF && i + 1 < in.size() && (in[i + 1] == 0x00 || in[i + 1] >= 0xE0)) {
            out.push_back(0x00);
        }
    }
    return out;
}

void writeBytes(const fs::path& path, const ByteVec& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

fs::path au2TempPath(const char* suffix) {
    return fs::temp_directory_path() /
           ("koalaamp_au2_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
            suffix);
}

// Independent s16 WAV writer (mirrors the seam test's helper but stays local to
// the AU-2 cases so the two blocks are not coupled).
void writeSineWavAu2(const fs::path& path, double hz, unsigned rate, double seconds) {
    std::vector<std::int16_t> pcm;
    const auto frames = static_cast<unsigned>(rate * seconds);
    for (unsigned i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / rate;
        pcm.push_back(static_cast<std::int16_t>(
            std::lround(0.5 * 32767.0 * std::sin(2.0 * std::numbers::pi * hz * t))));
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
    u16(2);
    u32(rate);
    u32(rate * 4);
    u16(4);
    u16(16);
    f.write("data", 4);
    u32(dataBytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
}

} // namespace

TEST_CASE("RgTags: synthetic FLAC Vorbis comments", "[au2][dsp][rgtags]") {
    const ByteVec flac = flacWithVorbisComment({"REPLAYGAIN_TRACK_GAIN=-7.50 dB",
                                                "REPLAYGAIN_TRACK_PEAK=0.987",
                                                "REPLAYGAIN_ALBUM_GAIN=+2.00 dB"});
    const RgTagRead r = parseReplayGainTags(flac.data(), flac.size());
    CHECK(r.any());
    CHECK(r.container == RgTagContainer::VorbisComment);
    REQUIRE(r.info.trackGainDb.has_value());
    REQUIRE(r.info.trackPeak.has_value());
    REQUIRE(r.info.albumGainDb.has_value());
    CHECK(r.info.trackGainDb.value() == Catch::Approx(-7.5));
    CHECK(r.info.trackPeak.value() == Catch::Approx(0.987));
    CHECK(r.info.albumGainDb.value() == Catch::Approx(2.0));
    CHECK_FALSE(r.info.albumPeak.has_value());
    CHECK_FALSE(r.info.trackLufs.has_value());
}

TEST_CASE("RgTags: synthetic Ogg-Vorbis and Ogg-Opus pages", "[au2][dsp][rgtags]") {
    SECTION("Vorbis comment packet (keys may be any case)") {
        const ByteVec ogg =
            oggPage(0x02, {vorbisIdPacket(),
                           vorbisCommentPacket({"replaygain_track_gain=-7.50 dB",
                                                "REPLAYGAIN_TRACK_PEAK=0.987",
                                                "ReplayGain_Album_Gain=+2.00 dB"})});
        const RgTagRead r = parseReplayGainTags(ogg.data(), ogg.size());
        CHECK(r.any());
        CHECK(r.container == RgTagContainer::VorbisComment);
        CHECK(r.info.trackGainDb.value() == Catch::Approx(-7.5));
        CHECK(r.info.trackPeak.value() == Catch::Approx(0.987));
        CHECK(r.info.albumGainDb.value() == Catch::Approx(2.0));
    }
    SECTION("OpusTags with the R128 Q7.8 form") {
        const ByteVec ogg = oggPage(0x02, {opusIdPacket(),
                                           opusCommentPacket({"R128_TRACK_GAIN=-512",
                                                              "R128_ALBUM_GAIN=256"})});
        const RgTagRead r = parseReplayGainTags(ogg.data(), ogg.size());
        CHECK(r.any());
        CHECK(r.container == RgTagContainer::VorbisComment);
        CHECK(r.info.trackGainDb.value() == Catch::Approx(-2.0)); // -512 / 256
        CHECK(r.info.albumGainDb.value() == Catch::Approx(1.0)); // 256 / 256
    }
    SECTION("R128 falls back to a dB string when not a plain integer") {
        const ByteVec ogg =
            oggPage(0x02, {opusIdPacket(), opusCommentPacket({"R128_TRACK_GAIN=-3.5 dB"})});
        const RgTagRead r = parseReplayGainTags(ogg.data(), ogg.size());
        CHECK(r.any());
        CHECK(r.info.trackGainDb.value() == Catch::Approx(-3.5));
    }
}

TEST_CASE("RgTags: synthetic ID3v2 frames", "[au2][dsp][rgtags]") {
    SECTION("v2.4 UTF-8 TXXX") {
        const ByteVec id3 = id3File(
            4, 0x00,
            id3Frame24("TXXX", txxxPayload(0x03, "REPLAYGAIN_TRACK_GAIN", "-7.50 dB")));
        const RgTagRead r = parseReplayGainTags(id3.data(), id3.size());
        CHECK(r.any());
        CHECK(r.container == RgTagContainer::Id3v2);
        CHECK(r.info.trackGainDb.value() == Catch::Approx(-7.5));
    }
    SECTION("v2.3 UTF-16 description and value (encoding 1)") {
        const ByteVec id3 = id3File(
            3, 0x00, id3Frame23("TXXX", txxxPayloadUtf16Le("REPLAYGAIN_ALBUM_GAIN", "+2.00 dB")));
        const RgTagRead r = parseReplayGainTags(id3.data(), id3.size());
        CHECK(r.any());
        CHECK(r.container == RgTagContainer::Id3v2);
        CHECK(r.info.albumGainDb.value() == Catch::Approx(2.0));
    }
    SECTION("v2.4 header unsynchronisation is undone before the frames are read") {
        // A leading TIT2 frame carries 0xFF 0xE0, written on disk as 0xFF 0x00
        // 0xE0; if the region were not de-unsynchronised the TXXX frame after it
        // would be misaligned and lost.
        ByteVec frames = id3Frame24("TIT2", ByteVec{0x00, 'x', 0xFF, 0xE0});
        const ByteVec txxx =
            id3Frame24("TXXX", txxxPayload(0x03, "REPLAYGAIN_TRACK_GAIN", "-7.50 dB"));
        frames.insert(frames.end(), txxx.begin(), txxx.end());
        const ByteVec disk = synchronise(frames);
        const ByteVec id3 = id3File(4, 0x80, disk);
        const RgTagRead r = parseReplayGainTags(id3.data(), id3.size());
        CHECK(r.any());
        CHECK(r.container == RgTagContainer::Id3v2);
        CHECK(r.info.trackGainDb.value() == Catch::Approx(-7.5));
    }
    SECTION("R128 Q7.8 gain in a vorbis comment (the Opus tag form)") {
        // Already covered synthetically; this is the *other* accepted form of the
        // same key: a plain dB string in R128_TRACK_GAIN (some taggers write it).
        const ByteVec flac = flacWithVorbisComment({"R128_TRACK_GAIN=+1.50 dB"});
        const RgTagRead r = parseReplayGainTags(flac.data(), flac.size());
        CHECK(r.any());
        CHECK(r.info.trackGainDb.value() == Catch::Approx(1.5));
    }

    SECTION("an unrelated frame is ignored (TXXX is enough for ReplayGain)") {
        const ByteVec id3 = id3File(4, 0x00,
                                    id3Frame24("TIT2", ByteVec{0x00, 'h', 'i'}));
        const RgTagRead r = parseReplayGainTags(id3.data(), id3.size());
        CHECK_FALSE(r.any());
        CHECK(r.container == RgTagContainer::None);
    }
}

TEST_CASE("RgTags: malformed input is rejected without reading out of bounds",
          "[au2][dsp][rgtags]") {
    SECTION("null and empty") {
        CHECK_FALSE(parseReplayGainTags(nullptr, 0).any());
        const ByteVec empty;
        CHECK_FALSE(parseReplayGainTags(empty.data(), empty.size()).any());
    }
    SECTION("three-byte garbage") {
        const ByteVec g{'a', 'b', 'c'};
        const RgTagRead r = parseReplayGainTags(g.data(), g.size());
        CHECK_FALSE(r.any());
        CHECK(r.container == RgTagContainer::None);
    }
    SECTION("four-byte garbage") {
        const ByteVec g{'g', 'a', 'r', 'b'};
        CHECK_FALSE(parseReplayGainTags(g.data(), g.size()).any());
    }
    SECTION("FLAC block declaring a huge (0xFFFFFF) length") {
        ByteVec b;
        pushBytes(b, "fLaC");
        b.push_back(0x80 | 4);
        pushU24BE(b, 0xFFFFFF); // 16 MiB >> buffer
        pushBytes(b, "VENDOR"); // far less than declared
        CHECK_FALSE(parseReplayGainTags(b.data(), b.size()).any());
    }
    SECTION("ID3 header declaring a size larger than the buffer") {
        ByteVec b;
        pushBytes(b, "ID3");
        b.push_back(4);
        b.push_back(0);
        b.push_back(0);
        pushSyncSafe(b, 100000);
        pushBytes(b, "TXXX");
        CHECK_FALSE(parseReplayGainTags(b.data(), b.size()).any());
    }
    SECTION("Ogg page whose lacing sum exceeds the page") {
        ByteVec b;
        pushBytes(b, "OggS");
        b.push_back(0);
        b.push_back(0x02);
        for (int i = 0; i < 8; ++i) {
            b.push_back(0);
        }
        pushU32LE(b, 0);
        pushU32LE(b, 0);
        pushU32LE(b, 0);
        b.push_back(1);      // one segment
        b.push_back(200);    // claims 200 payload bytes
        pushBytes(b, "OggS"); // only 4 present
        CHECK_FALSE(parseReplayGainTags(b.data(), b.size()).any());
    }
    SECTION("truncated Vorbis comment entry") {
        ByteVec b;
        pushBytes(b, "fLaC");
        ByteVec vc;
        pushU32LE(vc, 0);    // vendor length 0
        pushU32LE(vc, 1);    // count 1
        pushU32LE(vc, 4096); // entry length beyond the declared block
        b.push_back(0x80 | 4);
        pushU24BE(b, static_cast<std::uint32_t>(vc.size()));
        b.insert(b.end(), vc.begin(), vc.end());
        CHECK_FALSE(parseReplayGainTags(b.data(), b.size()).any());
    }
}

TEST_CASE("RgTags: file overload and RgTagSource cache", "[au2][dsp][rgtags]") {
    const fs::path flac = au2TempPath("tags.flac");
    writeBytes(flac, flacWithVorbisComment({"REPLAYGAIN_TRACK_GAIN=-7.50 dB",
                                            "REPLAYGAIN_TRACK_PEAK=0.987"}));

    const RgTagRead read = readReplayGainTags(flac.string());
    CHECK(read.any());
    CHECK(read.info.trackGainDb.value() == Catch::Approx(-7.5));
    CHECK(read.info.trackPeak.value() == Catch::Approx(0.987));

    RgTagSource source;
    CHECK(source.size() == 0);
    const RgInfo info = source.forPath(flac.string());
    CHECK(info.trackGainDb.value() == Catch::Approx(-7.5));
    CHECK(source.size() == 1);
    const RgInfo cached = source.forPath(flac.string()); // second read is cached
    CHECK(cached.trackGainDb.value() == Catch::Approx(-7.5));
    CHECK(source.size() == 1);

    // A missing file yields an empty RgInfo (and is cached as such).
    const RgInfo missing = source.forPath((flac.string() + ".nope"));
    CHECK_FALSE(missing.trackGainDb.has_value());
    CHECK(source.size() == 2);

    source.clear();
    CHECK(source.size() == 0);

    CHECK_FALSE(readReplayGainTags((flac.string() + ".nope")).any());

    std::error_code ec;
    fs::remove(flac, ec);
}

TEST_CASE("RgTags: real files written by ffmpeg", "[au2][dsp][rgtags][ffmpeg]") {
    if (std::system("command -v ffmpeg >/dev/null 2>&1") != 0) {
        SKIP("ffmpeg not on PATH — real-file ReplayGain cases skipped");
    }

    const fs::path dir = au2TempPath("ffmpeg");
    fs::create_directories(dir);
    const fs::path wav = dir / "in.wav";
    writeSineWavAu2(wav, 1000.0, 48000, 1.0);

    // ffmpeg writes unknown keys verbatim: Vorbis comments for FLAC/Ogg and
    // (verified by dumping the bytes) ID3v2.4 TXXX frames for MP3.
    auto encode = [&](const std::string& out) {
        const std::string cmd = "ffmpeg -y -loglevel error -i \"" + wav.string() +
                                "\" -metadata REPLAYGAIN_TRACK_GAIN=\"-7.50 dB\""
                                " -metadata REPLAYGAIN_TRACK_PEAK=0.987"
                                " -metadata REPLAYGAIN_ALBUM_GAIN=\"+2.00 dB\" \"" +
                                (dir / out).string() + "\" >/dev/null 2>&1";
        return std::system(cmd.c_str()) == 0;
    };

    SECTION("FLAC (Vorbis comments)") {
        REQUIRE(encode("out.flac"));
        const RgTagRead r = readReplayGainTags((dir / "out.flac").string());
        CHECK(r.container == RgTagContainer::VorbisComment);
        CHECK(r.info.trackGainDb.value() == Catch::Approx(-7.5));
        CHECK(r.info.trackPeak.value() == Catch::Approx(0.987));
        CHECK(r.info.albumGainDb.value() == Catch::Approx(2.0));
    }
    SECTION("Ogg-Vorbis (Vorbis comments)") {
        REQUIRE(encode("out.ogg"));
        const RgTagRead r = readReplayGainTags((dir / "out.ogg").string());
        CHECK(r.container == RgTagContainer::VorbisComment);
        CHECK(r.info.trackGainDb.value() == Catch::Approx(-7.5));
        CHECK(r.info.trackPeak.value() == Catch::Approx(0.987));
        CHECK(r.info.albumGainDb.value() == Catch::Approx(2.0));
    }
    SECTION("MP3 (ID3v2.4 TXXX frames)") {
        REQUIRE(encode("out.mp3"));
        const RgTagRead r = readReplayGainTags((dir / "out.mp3").string());
        CHECK(r.container == RgTagContainer::Id3v2);
        CHECK(r.info.trackGainDb.value() == Catch::Approx(-7.5));
        CHECK(r.info.trackPeak.value() == Catch::Approx(0.987));
        CHECK(r.info.albumGainDb.value() == Catch::Approx(2.0));
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("RgTags: a nonexistent path reads as empty", "[au2][dsp][rgtags]") {
    const std::string path =
        (fs::temp_directory_path() / "koalaamp_au2_does_not_exist.flac").string();
    const RgTagRead r = readReplayGainTags(path);
    CHECK_FALSE(r.any());
    CHECK(r.container == RgTagContainer::None);
}
