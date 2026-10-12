// SPDX-License-Identifier: MIT
// AU-2 — gapless boundary + ReplayGain activation tests
// (docs/tickets/AU-2-gapless-replaygain.md).
//
// What is asserted here, from the engine chain's own numbers:
//   * the DSP-path gapless boundary is sample-exact: across N transitions the
//     rendered PCM is the concatenation of the source tracks' samples, with no
//     samples dropped, discarded or replaced by silence;
//   * the compact ring's inability to hold libvlc's decode-ahead burst is
//     measured and bounded (the spec §3.1 caveat), not left implicit;
//   * an explicit discontinuity (seek, next) still drops the stale ring;
//   * ReplayGain activation from a real track's tags: the applied gain measured
//     tagged vs untagged, plus mode/preamp/fallback/clip through the real
//     control surface.
//
// Fixtures: WAV synthesis in-process (no fetch). The RG section encodes
// ReplayGain-tagged FLAC/OGG/MP3 with the system ffmpeg when present and skips
// otherwise. Output device: the miniaudio null backend (real device thread +
// data callback, no hardware).
//
// Measured engine behaviour this builds on (AU-2 spike, docs/spikes/au2-gapless.md):
//   * libvlc hands its decode-ahead to the callback in one burst as soon as a
//     media is activated (~1.15 s here; FE-4 measured ~0.87-1.15 s);
//   * the aout flush callback fires at end-of-input *and* on a seek. FE-2/FE-4
//     dropped the ring there, which threw away the last ~1 s of every track —
//     the dominant boundary defect. The ring is now cleared only where the core
//     decides a discontinuity;
//   * EndReached arrives when the input is done, ~1 s before the ring's tail has
//     been rendered. Swapping at EndReached loses the incoming track's burst (it
//     does not fit beside the tail); swapping once the tail has drained to
//     `gaplessTailMs` loses nothing.

#include "audio/DspPipeline.hpp"
#include "audio/ReplayGain.hpp"
#include "playback/PlaybackCore.hpp"

#if defined(KOALAAMP_AU2_HAS_RGTAGS)
#include "audio/RgTags.hpp"
#endif

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using koalaamp::audio::DspPipeline;
using koalaamp::audio::DspSettings;
using koalaamp::audio::RgMode;
using koalaamp::playback::MediaRef;
using koalaamp::playback::PlaybackConfig;
using koalaamp::playback::PlaybackCore;
using koalaamp::playback::RepeatMode;
namespace fs = std::filesystem;

constexpr unsigned kRate = 48000;
constexpr unsigned kCh = 2;

// --- fixtures --------------------------------------------------------------

class FixtureDir {
public:
    FixtureDir() {
        m_dir = fs::temp_directory_path() /
                ("koalaamp_au2_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(m_dir);
    }
    ~FixtureDir() {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }
    FixtureDir(const FixtureDir&) = delete;
    FixtureDir& operator=(const FixtureDir&) = delete;
    fs::path file(const std::string& name) const { return m_dir / name; }

private:
    fs::path m_dir;
};

/// 16-bit stereo sine WAV. `phase0` lets track N+1 continue track N's phase, so
/// a sample-exact concatenation is one continuous sine.
fs::path writeSineWav(const fs::path& path, double hz, double seconds, double phase0 = 0.0,
                      double amplitude = 0.5) {
    std::vector<std::int16_t> pcm;
    const auto frames = static_cast<unsigned>(std::lround(kRate * seconds));
    pcm.reserve(static_cast<std::size_t>(frames) * kCh);
    for (unsigned i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kRate;
        const auto v = static_cast<std::int16_t>(
            std::lround(amplitude * 32767.0 * std::sin(2.0 * std::numbers::pi * hz * t + phase0)));
        for (unsigned c = 0; c < kCh; ++c) {
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
    u16(kCh);
    u32(kRate);
    u32(kRate * kCh * 2);
    u16(kCh * 2);
    u16(16);
    f.write("data", 4);
    u32(dataBytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
    return path;
}

/// The samples a sine fixture holds, as the tap delivers them (float, /32768).
std::vector<float> referenceSine(double hz, unsigned frames, double phase0 = 0.0,
                                 double amplitude = 0.5) {
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(frames) * kCh);
    for (unsigned i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kRate;
        const auto v = static_cast<std::int16_t>(
            std::lround(amplitude * 32767.0 * std::sin(2.0 * std::numbers::pi * hz * t + phase0)));
        const float f = static_cast<float>(v) / 32768.0f;
        for (unsigned c = 0; c < kCh; ++c) {
            out.push_back(f);
        }
    }
    return out;
}

bool haveFfmpeg() {
    return std::system("command -v ffmpeg >/dev/null 2>&1") == 0;
}

PlaybackConfig nullOutput(PlaybackConfig::RingRegime ring, bool gapless = true) {
    PlaybackConfig config;
    config.output = PlaybackConfig::Output::Null;
    config.ring = ring;
    config.gapless = gapless;
    config.prefetchLeadMs = 10000;
    return config;
}

template <typename Pred> bool waitFor(Pred pred, std::chrono::milliseconds timeout = 20000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return pred();
}

/// Captures every block the DSP seam sees (post-DSP, i.e. exactly what the ring
/// receives) and means the boundary can be checked sample by sample.
class Capture {
public:
    void onPcm(const float* interleaved, std::size_t frames) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const std::size_t n = frames * kCh;
        m_pcm.insert(m_pcm.end(), interleaved, interleaved + n);
        m_frames.fetch_add(frames);
    }
    std::vector<float> pcm() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pcm;
    }
    std::uint64_t frames() const { return m_frames.load(); }

private:
    mutable std::mutex m_mutex;
    std::vector<float> m_pcm;
    std::atomic<std::uint64_t> m_frames{0};
};

/// Longest run of consecutive zero frames: a boundary that fell back to silence
/// shows up here (the 440 Hz source sine only touches 0 one frame at a time).
std::size_t longestZeroRunFrames(const std::vector<float>& pcm) {
    std::size_t run = 0;
    std::size_t worst = 0;
    for (std::size_t i = 0; i + kCh <= pcm.size(); i += kCh) {
        bool zero = true;
        for (unsigned c = 0; c < kCh; ++c) {
            if (pcm[i + c] != 0.0f) {
                zero = false;
                break;
            }
        }
        run = zero ? run + 1 : 0;
        worst = std::max(worst, run);
    }
    return worst;
}

std::size_t countMismatches(const std::vector<float>& got, const std::vector<float>& expected,
                            std::size_t at, std::size_t* firstAt = nullptr) {
    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (got[at + i] != expected[i]) {
            if (mismatches == 0 && firstAt != nullptr) {
                *firstAt = at + i;
            }
            ++mismatches;
        }
    }
    return mismatches;
}

} // namespace

// ===========================================================================
//  gapless boundary (DSP path)
// ===========================================================================

TEST_CASE("AU-2: gapless boundary is sample-exact on the DSP path", "[au2][core][gapless]") {
    constexpr unsigned kTrackFrames = 57600; // 1.2 s per track
    constexpr unsigned kTracks = 24;

    FixtureDir fixtures;
    // One continuous 440 Hz sine split into 24 tracks, each starting where the
    // previous ended: a sample-exact concat is a phase-continuous sine, so any
    // dropped or silent frame is a measurable discontinuity.
    const double hz = 440.0;
    const double trackSeconds = static_cast<double>(kTrackFrames) / kRate;
    std::vector<fs::path> paths;
    for (unsigned t = 0; t < kTracks; ++t) {
        const double phase = 2.0 * std::numbers::pi * hz * trackSeconds * static_cast<double>(t);
        paths.push_back(writeSineWav(fixtures.file("t" + std::to_string(t) + ".wav"), hz,
                                     trackSeconds, phase));
    }

    DspPipeline dsp;
    DspSettings settings;
    settings.enabled = true; // every stage transparent; the boundary is the subject
    dsp.setSettings(settings);
    Capture capture;
    std::atomic<int> prepares{0}; // captured by the prepare hook: outlives the core
    // Declared AFTER the transform: the core's PCM seam invokes the transform
    // from libvlc's audio-output thread, so the transform must outlive the core
    // (PlaybackCore.hpp lifetime contract; the core's destructor quiesces the
    // seam before releasing the tap).
    PlaybackCore core(nullOutput(PlaybackConfig::RingRegime::Relaxed));
    core.setPcmCallback(dsp.makeCallback(
        [&](const float* f, std::size_t frames, const koalaamp::playback::PcmFormat&) {
            capture.onPcm(f, frames);
        }));
    core.setTrackPrepareHook([&](const MediaRef&) { prepares.fetch_add(1); });
    core.setPrefetchHook([&](const MediaRef&) {});

    for (unsigned t = 1; t < kTracks; ++t) {
        core.enqueue(MediaRef{MediaRef::Kind::Path, paths[t].string()});
    }
    core.play(MediaRef{MediaRef::Kind::Path, paths[0].string()});

    // Measure from after the first track's activation: that start-up silence is
    // engine start-up, not a boundary (FE-4 finding 1 documents it separately).
    REQUIRE(waitFor([&] { return core.metrics().framesRendered > 2 * kRate; }));
    const auto baseline = core.metrics();

    const bool finished = waitFor([&] { return core.metrics().gaplessHandovers >= kTracks - 1; },
                                  60s);
    INFO("handovers=" << core.metrics().gaplessHandovers
                      << " rendered=" << core.metrics().framesRendered
                      << " decoded=" << core.metrics().framesDecoded);
    REQUIRE(finished);
    // Let the last track be delivered before reading the capture (its decode
    // lead means `framesDecoded` passes the total track length).
    REQUIRE(waitFor([&] { return core.metrics().framesDecoded >= 1ULL * kTrackFrames * kTracks; },
                    30s));
    const auto after = core.metrics();
    std::this_thread::sleep_for(200ms);
    const std::vector<float> pcm = capture.pcm();

    // --- no audio lost at any of the 23 boundaries ------------------------
    CHECK(after.gaplessHandovers == kTracks - 1);
    CHECK(after.framesDiscarded == baseline.framesDiscarded); // nothing thrown away
    CHECK(after.framesDropped == baseline.framesDropped);     // ring never overflowed
    CHECK(after.silenceFrames == baseline.silenceFrames);     // no gap opened
    CHECK(after.framesDecoded >= 1ULL * kTrackFrames * kTracks);
    CHECK(prepares.load() == static_cast<int>(kTracks)); // RG seam fired per track

    // --- the rendered stream is the concatenation of the tracks -----------
    const std::size_t boundary = static_cast<std::size_t>(kTrackFrames) * kCh;
    REQUIRE(pcm.size() >= boundary * kTracks);
    const std::vector<float> first = referenceSine(hz, kTrackFrames);
    // Track 0's samples appear verbatim at the head of the capture...
    CHECK(std::equal(first.begin(), first.end(), pcm.begin()));
    // ...and every later track continues exactly where the previous one ended.
    std::size_t mismatches = 0;
    std::size_t firstMismatch = 0;
    for (unsigned t = 1; t < kTracks; ++t) {
        const double phase = 2.0 * std::numbers::pi * hz * trackSeconds * static_cast<double>(t);
        const std::vector<float> expected = referenceSine(hz, kTrackFrames, phase);
        mismatches += countMismatches(pcm, expected, static_cast<std::size_t>(t) * boundary,
                                      &firstMismatch);
    }
    INFO("mismatched samples=" << mismatches << " first at " << firstMismatch << " of "
                               << pcm.size());
    CHECK(mismatches == 0);
    CHECK(longestZeroRunFrames(pcm) <= 1);
    core.stop();
}

TEST_CASE("AU-2: the compact ring cannot hold libvlc's decode burst (spec 3.1 caveat)",
          "[au2][core][gapless][ring]") {
    // The gapless boundary needs free ring for the incoming track's decode-ahead
    // (~1.15 s measured). The compact 200 ms request resolves to 341 ms, so the
    // burst cannot be absorbed anywhere: libvlc's excess is dropped. This pins
    // the measurement behind the relaxed default (PlaybackConfig).
    constexpr unsigned kTrackFrames = 57600;
    FixtureDir fixtures;
    const fs::path a = writeSineWav(fixtures.file("a.wav"), 440.0, 1.2);
    const fs::path b = writeSineWav(fixtures.file("b.wav"), 440.0, 1.2);

    DspPipeline dsp;
    DspSettings settings;
    settings.enabled = true;
    dsp.setSettings(settings);
    PlaybackCore core(nullOutput(PlaybackConfig::RingRegime::Compact));
    core.setPcmCallback(dsp.makeCallback({}));
    core.setTrackPrepareHook([&](const MediaRef&) {});
    core.setPrefetchHook([&](const MediaRef&) {});
    core.enqueue(MediaRef{MediaRef::Kind::Path, b.string()});
    core.play(MediaRef{MediaRef::Kind::Path, a.string()});

    REQUIRE(waitFor([&] { return core.metrics().framesDecoded > 0; }));
    REQUIRE(waitFor([&] { return core.metrics().gaplessHandovers >= 1; }, 30s));
    REQUIRE(waitFor([&] { return core.metrics().framesDecoded >= 2ULL * kTrackFrames; }, 30s));
    const auto m = core.metrics();
    INFO("ring=" << m.ringFrames << " frames, dropped=" << m.framesDropped
                 << " discarded=" << m.framesDiscarded);
    CHECK(m.ringFrames <= 16384);            // 341 ms at 48 kHz
    CHECK(m.framesDropped > 0);              // the burst did not fit
    CHECK(m.framesDropped > kRate / 2);      // > 0.5 s of audio, not a rounding wobble
    core.stop();
}

TEST_CASE("AU-2: an explicit discontinuity still drops the stale ring", "[au2][core][seek]") {
    FixtureDir fixtures;
    const fs::path a = writeSineWav(fixtures.file("a.wav"), 440.0, 4.0);
    DspPipeline dsp;
    DspSettings settings;
    settings.enabled = true;
    dsp.setSettings(settings);
    PlaybackCore core(nullOutput(PlaybackConfig::RingRegime::Relaxed));
    core.setPcmCallback(dsp.makeCallback({}));
    core.play(MediaRef{MediaRef::Kind::Path, a.string()});

    // Seek well into the ring's tail: the pre-seek frames are invalid and must
    // be dropped (the gapless path preserves the ring; a seek must not).
    REQUIRE(waitFor([&] { return core.metrics().framesRendered > kRate; }, 20s));
    const auto before = core.metrics();
    CHECK(before.framesDiscarded == 0);
    core.seekMs(3000);
    REQUIRE(waitFor([&] { return core.metrics().framesDiscarded > before.framesDiscarded; }, 5s));
    const auto after = core.metrics();
    INFO("discarded=" << after.framesDiscarded << " (seek dropped the stale tail)");
    CHECK(after.framesDiscarded > before.framesDiscarded);
    // ...and playback keeps flowing from the new position.
    REQUIRE(waitFor([&] { return core.metrics().framesRendered > after.framesRendered + kRate; },
                    10s));
    core.stop();
}

// ===========================================================================
//  ReplayGain activation (AU-1's stage, populated from real track tags)
// ===========================================================================

#if defined(KOALAAMP_AU2_HAS_RGTAGS)

namespace {

using koalaamp::audio::RgTagSource;

/// Encode `wav` to FLAC with the given `-metadata` arguments (lossless, so the
/// decoded samples are the WAV's own). Returns false when ffmpeg is missing.
bool encodeFlac(const fs::path& wav, const fs::path& out, const std::string& metadata) {
    const std::string cmd = "ffmpeg -y -loglevel error -i \"" + wav.string() + "\" " + metadata +
                            " \"" + out.string() + "\" >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

/// RMS of one channel of an interleaved capture.
double rmsChannel0(const std::vector<float>& pcm) {
    if (pcm.size() < 2) {
        return 0.0;
    }
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i + 1 < pcm.size(); i += kCh) {
        sum += static_cast<double>(pcm[i]) * pcm[i];
        ++n;
    }
    return n != 0 ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
}

double peakAbs(const std::vector<float>& pcm) {
    double peak = 0.0;
    for (const float f : pcm) {
        peak = std::max(peak, std::abs(static_cast<double>(f)));
    }
    return peak;
}

/// One track through the core with the DSP installed, capturing the PCM and the
/// stage's state. Waits only until the stage has processed its first block(s).
struct RgRun {
    std::vector<float> pcm;
    koalaamp::audio::DspPipeline::Stats stats;
};

std::optional<RgRun> runTrack(PlaybackConfig::RingRegime ring, const fs::path& track,
                              RgMode mode, double preampDb = 0.0, bool preventClip = true) {
    DspPipeline dsp;
    DspSettings settings;
    settings.enabled = true;
    settings.rg.mode = mode;
    settings.rg.preampDb = preampDb;
    settings.rg.preventClip = preventClip;
    settings.rg.fallbackDb = -6.0;
    dsp.setSettings(settings);
    RgTagSource tags;
    Capture capture;
    PlaybackCore core(nullOutput(ring));
    // The wiring layer AU-2 documents for SE-2: read the track's tags and hand
    // them to the stage before the track's first sample is decoded.
    core.setTrackPrepareHook([&](const MediaRef& media) {
        dsp.setReplayGain(tags.forPath(media.value));
    });
    core.setPcmCallback(dsp.makeCallback(
        [&](const float* f, std::size_t frames, const koalaamp::playback::PcmFormat&) {
            capture.onPcm(f, frames);
        }));
    core.play(MediaRef{MediaRef::Kind::Path, track.string()});
    const bool got = waitFor([&] { return dsp.stats().processedBlocks >= 4; }, 20s);
    if (!got) {
        core.stop();
        return std::nullopt;
    }
    RgRun run;
    run.stats = dsp.stats();
    run.pcm = capture.pcm();
    core.stop();
    return run;
}

} // namespace

TEST_CASE("AU-2: ReplayGain activation measures the applied gain (tagged vs untagged)",
          "[au2][core][rg]") {
    if (!haveFfmpeg()) {
        SKIP("ffmpeg not on PATH — ReplayGain tag activation cases skipped");
    }
    constexpr double kAmplitude = 0.5;
    FixtureDir fixtures;
    const fs::path src = writeSineWav(fixtures.file("src.wav"), 440.0, 0.5, 0.0, kAmplitude);
    const fs::path tagged = fixtures.file("tagged.flac");
    const fs::path untagged = fixtures.file("untagged.flac");
    REQUIRE(encodeFlac(src, tagged, "-metadata REPLAYGAIN_TRACK_GAIN=\"-3.00 dB\""));
    REQUIRE(encodeFlac(src, untagged, ""));

    // Untagged → the −6 dB fallback (spec §4.3); tagged → the file's own gain.
    const auto plain = runTrack(PlaybackConfig::RingRegime::Relaxed, untagged, RgMode::Track);
    REQUIRE(plain.has_value());
    CHECK(plain->stats.rgFallback);
    CHECK(plain->stats.gainDb == Catch::Approx(-6.0));

    const auto taggedRun = runTrack(PlaybackConfig::RingRegime::Relaxed, tagged, RgMode::Track);
    REQUIRE(taggedRun.has_value());
    CHECK_FALSE(taggedRun->stats.rgFallback);
    CHECK(taggedRun->stats.gainDb == Catch::Approx(-3.0));

    // The stage's reported gain must be what the PCM actually got: +3 dB of
    // headroom on the untagged file. (Δ of the two measured RMS values.)
    const double plainRms = rmsChannel0(plain->pcm);
    const double taggedRms = rmsChannel0(taggedRun->pcm);
    const double deltaDb = 20.0 * std::log10(taggedRms / plainRms);
    INFO("plainRms=" << plainRms << " taggedRms=" << taggedRms << " delta=" << deltaDb);
    CHECK(deltaDb == Catch::Approx(3.0).margin(0.5)); // ticket: ±6 dB tags, ±0.5 dB

    // ...and the absolute figure agrees with the tag, not just the difference.
    const std::vector<float> reference = referenceSine(440.0, 24000, 0.0, kAmplitude);
    const double referenceRms = rmsChannel0(reference);
    const double absoluteDb = 20.0 * std::log10(taggedRms / referenceRms);
    INFO("absolute=" << absoluteDb << " dB vs tag -3.00 dB");
    CHECK(absoluteDb == Catch::Approx(-3.0).margin(0.5));

    // Per-track re-activation: a second track's tags replace the first's.
    const fs::path louder = fixtures.file("louder.flac");
    REQUIRE(encodeFlac(src, louder, "-metadata REPLAYGAIN_TRACK_GAIN=\"+2.00 dB\""));
    const auto louderRun = runTrack(PlaybackConfig::RingRegime::Relaxed, louder, RgMode::Track);
    REQUIRE(louderRun.has_value());
    CHECK(louderRun->stats.gainDb == Catch::Approx(2.0));
    CHECK(rmsChannel0(louderRun->pcm) > taggedRms);
}

TEST_CASE("AU-2: ReplayGain modes, preamp, fallback and clip prevention through the core",
          "[au2][core][rg]") {
    if (!haveFfmpeg()) {
        SKIP("ffmpeg not on PATH — ReplayGain tag activation cases skipped");
    }
    constexpr double kAmplitude = 0.9;
    FixtureDir fixtures;
    const fs::path src = writeSineWav(fixtures.file("both.wav"), 440.0, 0.5, 0.0, kAmplitude);
    const fs::path both = fixtures.file("both.flac");
    const fs::path none = fixtures.file("none.flac");
    // The tag peak must be the fixture's *real* sample peak (the 16-bit
    // quantisation makes it slightly lower than the nominal amplitude): the
    // guard's contract is "gain is clamped so declaredPeak * gain <= 1", so a
    // tag that under-declares the peak is a tag error, not a guard failure.
    const double fixturePeak = peakAbs(referenceSine(440.0, 24000, 0.0, kAmplitude));
    const std::string peakTag = std::to_string(fixturePeak);
    const double clipGainDb = -20.0 * std::log10(fixturePeak);
    REQUIRE(encodeFlac(src, both,
                       "-metadata REPLAYGAIN_TRACK_GAIN=\"-3.00 dB\""
                       " -metadata REPLAYGAIN_ALBUM_GAIN=\"-9.00 dB\""
                       " -metadata REPLAYGAIN_TRACK_PEAK=" + peakTag +
                       " -metadata REPLAYGAIN_ALBUM_PEAK=" + peakTag));
    REQUIRE(encodeFlac(src, none, ""));

    struct Case {
        const fs::path* track;
        RgMode mode;
        double preamp;
        bool preventClip;
        double expectGainDb;
        bool expectFallback;
        bool expectClipLimited;
        const char* what;
    };
    const std::vector<Case> cases = {
        {&both, RgMode::Off, 0.0, true, 0.0, false, false, "off: unity"},
        {&both, RgMode::Track, 0.0, true, -3.0, false, false, "track tag"},
        {&both, RgMode::Album, 0.0, true, -9.0, false, false, "album tag"},
        {&both, RgMode::Track, 2.0, true, -1.0, false, false, "track + 2 dB preamp"},
        {&none, RgMode::Track, 0.0, true, -6.0, true, false, "untagged -> -6 dB fallback"},
        {&none, RgMode::Album, 0.0, true, -6.0, true, false, "album on untagged -> fallback"},
        {&none, RgMode::Track, 3.0, true, -3.0, true, false, "fallback + preamp"},
        {&both, RgMode::Track, 6.0, true, clipGainDb, false, true,
         "peak-aware clip prevention"},
        {&both, RgMode::Track, 6.0, false, 3.0, false, false, "clip prevention off"},
    };

    for (const Case& c : cases) {
        INFO(c.what);
        const auto run =
            runTrack(PlaybackConfig::RingRegime::Relaxed, *c.track, c.mode, c.preamp, c.preventClip);
        REQUIRE(run.has_value());
        CHECK(run->stats.gainDb == Catch::Approx(c.expectGainDb).margin(0.02));
        CHECK(run->stats.rgFallback == c.expectFallback);
        CHECK(run->stats.rgClipLimited == c.expectClipLimited);
        if (c.preventClip) {
            // The ticket's acceptance: with prevention on, the output never
            // crosses full scale (peak tag 0.9 + 6 dB would be 1.8 without it).
            CHECK(peakAbs(run->pcm) <= 1.0 + 1e-6);
        }
    }
    // Without prevention the same setting *would* overdrive: proves the guard is
    // what held the level, not the fixture.
    const auto overdriven =
        runTrack(PlaybackConfig::RingRegime::Relaxed, both, RgMode::Track, 6.0, false);
    REQUIRE(overdriven.has_value());
    CHECK(peakAbs(overdriven->pcm) > 1.0);
}

TEST_CASE("AU-2: the prepare hook fires for every track, including a gapless handover",
          "[au2][core][rg][gapless]") {
    FixtureDir fixtures;
    const fs::path a = writeSineWav(fixtures.file("a.wav"), 440.0, 1.0);
    const fs::path b = writeSineWav(fixtures.file("b.wav"), 660.0, 4.0);
    std::mutex mutex;
    std::vector<std::string> prepared;
    PlaybackCore core(nullOutput(PlaybackConfig::RingRegime::Relaxed));
    core.setTrackPrepareHook([&](const MediaRef& media) {
        std::lock_guard<std::mutex> lock(mutex);
        prepared.push_back(media.value);
    });
    core.setPrefetchHook([&](const MediaRef&) {});
    core.enqueue(MediaRef{MediaRef::Kind::Path, b.string()});
    core.play(MediaRef{MediaRef::Kind::Path, a.string()});

    // The hook must fire before the engine starts the media: PCM cannot flow
    // before play(), and play() happens after the hook, so once any frame has
    // been decoded the track's prepare must already be recorded. That ordering
    // is what lets the DSP stage latch the right gain on its first block.
    REQUIRE(waitFor([&] { return core.metrics().framesDecoded > 0; }));
    {
        std::lock_guard<std::mutex> lock(mutex);
        REQUIRE(prepared.size() >= 1);
        CHECK(prepared[0] == a.string());
    }
    REQUIRE(waitFor([&] { return core.metrics().gaplessHandovers >= 1; }, 30s));
    {
        std::lock_guard<std::mutex> lock(mutex);
        REQUIRE(prepared.size() == 2);
        CHECK(prepared[1] == b.string()); // the handover prepares the incoming track too
    }
    core.stop();
}

TEST_CASE("AU-2: multi-track soak — the boundary path stays clean under load",
          "[au2][core][gapless][soak]") {
    // The ASAN/UBSAN acceptance run for the boundary path: a long chain of
    // tracks through the real DSP stage (ReplayGain + EQ doing work on every
    // sample), asserting per-boundary that nothing was discarded, dropped or
    // silenced, and that the stage's gain tracks each track's own tags.
    if (!haveFfmpeg()) {
        SKIP("ffmpeg not on PATH — the tagged soak needs it");
    }
    constexpr unsigned kTrackFrames = 96000;      // 2 s
    constexpr unsigned kTracks = 40;              // 39 gapless boundaries
    constexpr double kSeconds = 2.0;
    FixtureDir fixtures;

    std::vector<fs::path> paths;
    for (unsigned t = 0; t < kTracks; ++t) {
        const fs::path wav = writeSineWav(fixtures.file("soak" + std::to_string(t) + ".wav"), 440.0,
                                          kSeconds);
        // Alternate the tag: even tracks -4 dB, odd tracks +3 dB with a low
        // declared peak (so the clip guard engages on those).
        const std::string meta = (t % 2 == 0)
                                     ? "-metadata REPLAYGAIN_TRACK_GAIN=\"-4.00 dB\""
                                     : "-metadata REPLAYGAIN_TRACK_GAIN=\"+3.00 dB\""
                                       " -metadata REPLAYGAIN_TRACK_PEAK=0.5500";
        const fs::path flac = fixtures.file("soak" + std::to_string(t) + ".flac");
        REQUIRE(encodeFlac(wav, flac, meta));
        paths.push_back(flac);
    }

    DspPipeline dsp;
    DspSettings settings;
    settings.enabled = true;
    settings.rg.mode = RgMode::Track;
    settings.rg.preventClip = true;
    settings.eq.enabled = true;
    settings.eq.bands[4] = 24; // ~+3.5 dB @ 1 kHz: every sample does real work
    dsp.setSettings(settings);
    RgTagSource tags;
    Capture capture;
    PlaybackCore core(nullOutput(PlaybackConfig::RingRegime::Relaxed));
    core.setTrackPrepareHook([&](const MediaRef& media) {
        dsp.setReplayGain(tags.forPath(media.value));
    });
    core.setPrefetchHook([&](const MediaRef&) {});
    core.setPcmCallback(dsp.makeCallback(
        [&](const float* f, std::size_t frames, const koalaamp::playback::PcmFormat&) {
            capture.onPcm(f, frames);
        }));

    for (unsigned t = 1; t < kTracks; ++t) {
        core.enqueue(MediaRef{MediaRef::Kind::Path, paths[t].string()});
    }
    core.play(MediaRef{MediaRef::Kind::Path, paths[0].string()});

    REQUIRE(waitFor([&] { return core.metrics().framesRendered > kRate; }));
    const auto baseline = core.metrics();
    const bool finished =
        waitFor([&] { return core.metrics().gaplessHandovers >= kTracks - 1; }, 180s);
    INFO("handovers=" << core.metrics().gaplessHandovers
                      << " rendered=" << core.metrics().framesRendered
                      << " decoded=" << core.metrics().framesDecoded
                      << " dropped=" << core.metrics().framesDropped
                      << " discarded=" << core.metrics().framesDiscarded
                      << " silence=" << core.metrics().silenceFrames);
    REQUIRE(finished);
    REQUIRE(waitFor([&] { return core.metrics().framesDecoded >= 1ULL * kTrackFrames * kTracks; },
                    60s));
    const auto after = core.metrics();
    std::this_thread::sleep_for(200ms);
    INFO("lastBoundaryTailMs=" << after.lastBoundaryTailMs
                               << " ringFrames=" << after.ringFrames);

    CHECK(after.gaplessHandovers == kTracks - 1);
    CHECK(after.framesDiscarded == baseline.framesDiscarded);
    CHECK(after.framesDropped == baseline.framesDropped);
    CHECK(after.silenceFrames == baseline.silenceFrames);
    CHECK(dsp.stats().processedBlocks > 0);
    // The guard held across the whole chain: nothing over full scale.
    const std::vector<float> pcm = capture.pcm();
    REQUIRE(!pcm.empty());
    CHECK(peakAbs(pcm) <= 1.0 + 1e-6);
    // The stage's final gain must be the *last* track's own tag (+3 dB; the
    // declared 0.55 peak does not clamp a +3 dB gain — 0.55 * 1.41 < 1).
    INFO("final gain=" << dsp.stats().gainDb << " clipLimited=" << dsp.stats().rgClipLimited);
    CHECK(dsp.stats().gainDb == Catch::Approx(3.0).margin(0.02));
    core.stop();
}

#endif // KOALAAMP_AU2_HAS_RGTAGS

TEST_CASE("AU-2: repeat-one loops gaplessly (the replay plan goes through the boundary)",
          "[au2][core][gapless]") {
    FixtureDir fixtures;
    const fs::path a = writeSineWav(fixtures.file("loop.wav"), 440.0, 0.5);
    DspPipeline dsp;
    DspSettings settings;
    settings.enabled = true;
    dsp.setSettings(settings);
    PlaybackCore core(nullOutput(PlaybackConfig::RingRegime::Relaxed));
    core.setPcmCallback(dsp.makeCallback({}));
    core.setTrackPrepareHook([&](const MediaRef&) {});
    core.setPrefetchHook([&](const MediaRef&) {});
    core.setRepeat(RepeatMode::One);
    core.play(MediaRef{MediaRef::Kind::Path, a.string()});

    REQUIRE(waitFor([&] { return core.metrics().framesDecoded > 0; }));
    const auto baseline = core.metrics();
    REQUIRE(waitFor([&] { return core.metrics().gaplessHandovers >= 3; }, 40s));
    const auto after = core.metrics();
    INFO("handovers=" << after.gaplessHandovers << " discarded=" << after.framesDiscarded
                      << " dropped=" << after.framesDropped);
    CHECK(after.framesDiscarded == baseline.framesDiscarded); // the loop loses nothing
    CHECK(after.framesDropped == baseline.framesDropped);
    // The swap clears the published length and the periodic tick re-publishes it
    // from the new media (same file here): it must come back, not stay 0.
    CHECK(waitFor([&] { return core.metrics().lengthMs > 0; }, 5s));
    core.stop();
}
