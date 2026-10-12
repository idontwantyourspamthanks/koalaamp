// SPDX-License-Identifier: MIT
// AU-2 spike — what does a gapless track handover cost on the DSP path?
//
// The FE-4 core stop()s the tap and discardAll()s the ring at EndReached, so
// the boundary is a hard gap (the ring's ~1 s tail is thrown away and track B
// starts from libvlc's input-open latency). This probe measures the three
// candidate handovers against the real FE-2 primitives before AU-2 picks one:
//
//   s1  FE-4 baseline: stop + discard + setMedia(B) + play   (the gap to beat)
//   s2  same tap, keep the ring, but swap AT EndReached: measured worse than
//       `late` — the tail and the incoming burst are resident together and do
//       not fit, so libvlc drops the excess
//   late the shipped design: swap once the ring tail has drained to tailMs
//       (one decoder, one ring, no stop/discard)
//   s3  REJECTED alternative: second player pre-opened at prefetch, handoff at
//       EndReached; its PCM observer appends to the sink's ring behind a
//       single-writer gate.
//
// Output: per-run timeline + the seam at the boundary in frames (÷ 48 = ms):
// frames the sink served as silence + frames the ring discarded + frames the
// tap dropped. Run:  spike_au2 <s1|s2|late> [ringFloats] [tailMs]
//
// s3 (a second player pre-opened at the prefetch point, its PCM observer gated
// into the sink's ring) was probed and REJECTED as the product design: it needs
// a second sink source, a second DSP instance on another audio thread and a
// cross-ring switch, and its best case is no better than s2/late — see
// docs/spikes/au2-gapless.md. It is kept here because it is the alternative the
// ticket's brief suggested, and the measurement is the reason it was dropped.
#include "interop/vlc/MiniAudioSink.hpp"
#include "interop/vlc/VlcPcmTap.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace fs = std::filesystem;
using koalaamp::interop::vlc::FloatRing;
using koalaamp::interop::vlc::MiniAudioSink;
using koalaamp::interop::vlc::SinkBackend;
using koalaamp::interop::vlc::SinkConfig;
using koalaamp::interop::vlc::TapEvent;
using koalaamp::interop::vlc::VlcPcmTap;

namespace {

constexpr unsigned kRate = 48000;
constexpr unsigned kCh = 2;

/// ffmpeg-encode a WAV to FLAC in place (lossless: the decoded samples are the
/// WAV's own). Returns false when ffmpeg is missing or the encode fails.
bool encodeFlac(const fs::path& wav, const fs::path& out) {
    const std::string cmd =
        "ffmpeg -y -loglevel error -i \"" + wav.string() + "\" \"" + out.string() + "\" >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// 16-bit stereo sine WAV; `phase0` continues a previous track's phase for the
/// concatenation check.
void writeSineWav(const fs::path& path, double hz, double seconds, double phase0 = 0.0) {
    const auto frames = static_cast<unsigned>(kRate * seconds);
    std::vector<std::int16_t> pcm;
    pcm.reserve(static_cast<std::size_t>(frames) * kCh);
    for (unsigned i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kRate;
        const auto v = static_cast<std::int16_t>(
            std::lround(0.5 * 32767.0 * std::sin(2.0 * std::numbers::pi * hz * t + phase0)));
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
}

struct Probe {
    const std::string mode;
    std::string container{"wav"}; // "wav" | "flac" (the codec changes the burst)
    double floorMs{150.0};        // `late`: hand over once the outgoing tail <= this // "late": hand over when the outgoing ring tail <= this
    fs::path aPath, bPath;
    std::size_t ringFloats;

    std::unique_ptr<VlcPcmTap> a, b;
    std::unique_ptr<FloatRing> shared; // the sink's ring (s3)
    FloatRing* sinkRing{nullptr};     // what the sink reads (A's ring, or shared)
    std::unique_ptr<MiniAudioSink> sink;

    std::atomic<bool> endReached{false};
    std::atomic<bool> pendingLate{false};
    std::atomic<bool> bReady{false};
    std::atomic<int> owner{0}; // 0 = A, 1 = B (s3 gate)
    std::atomic<std::int64_t> endReachedAt{0};
    std::atomic<std::uint64_t> aFrames{0};
    std::atomic<std::uint64_t> bFrames{0};

    explicit Probe(std::string m, std::size_t ring) : mode(std::move(m)), ringFloats(ring) {}

    void setup() {
        const auto dir = fs::temp_directory_path() / "koalaamp_au2_probe";
        fs::create_directories(dir);
        const bool flac = container == "flac";
        const fs::path aw = dir / "a.wav";
        const fs::path bw = dir / "b.wav";
        aPath = flac ? dir / "a.flac" : aw;
        bPath = flac ? dir / "b.flac" : bw;
        writeSineWav(aw, 440.0, 4.0);
        writeSineWav(bw, 660.0, 4.0);
        if (flac) {
            encodeFlac(aw, aPath);
            encodeFlac(bw, bPath);
        }

        a = std::make_unique<VlcPcmTap>(ringFloats); // own ring = the sink ring for s1/s2
        if (mode == "s3") {
            shared = std::make_unique<FloatRing>(ringFloats);
        }
        sinkRing = mode == "s3" ? shared.get() : &a->ring();
        a->setEventListener([this](TapEvent event, std::int64_t, std::uint64_t) {
            if (event == TapEvent::EndReached) {
                endReachedAt.store(nowMs());
                endReached.store(true);
            }
        });
        a->setPcmObserver([this](const float* f, std::size_t frames, unsigned, unsigned) {
            aFrames.fetch_add(frames);
            // s3 decouples the tap's own ring: the observer is the sink-ring
            // writer, gated so A then B are the single writer in turn.
            if (mode == "s3" && owner.load() == 0) {
                shared->write(f, frames * kCh);
            }
        });

        sink = std::make_unique<MiniAudioSink>(*sinkRing);
        SinkConfig sc;
        sc.sampleRate = kRate;
        sc.channels = kCh;
        sc.periodFrames = 1024;
        sc.backend = SinkBackend::Null;
        if (!sink->start(sc)) {
            std::fprintf(stderr, "sink start failed\n");
            std::exit(2);
        }
    }

    /// s3: pre-open B on a second player (own instance) at the "prefetch" point,
    /// no playback yet. Its PCM observer feeds the shared ring once the owner
    /// gate flips.
    void prefetchB() {
        b = std::make_unique<VlcPcmTap>(8192);
        b->setPcmObserver([this](const float* f, std::size_t frames, unsigned, unsigned) {
            bFrames.fetch_add(frames);
            if (owner.load() == 1) {
                shared->write(f, frames * kCh);
            }
        });
        bReady.store(b->open(bPath.string()));
    }

    void handover() {
        const auto t = nowMs();
        if (mode == "late") {
            pendingLate.store(true);
            return; // the run loop waits for the ring to drain to floorMs
        }
        if (mode == "early") {
            // Honest sample-exact handover: swap *before* the decoder hands over
            // its end-of-track burst, so the outgoing track's ring tail and the
            // incoming track's derived burst are never resident together (they
            // do not fit: residual ~1.0 s + settle ~1.35 s > ring)
            a->setMedia(bPath.string());
            a->play();
        } else if (mode == "s1") {
            a->stop();
            sinkRing->discardAll();
            a->setMedia(bPath.string());
            a->play();
        } else if (mode == "s2") {
            // No stop, no discard: keep A's tail in the ring; hand the same
            // player the next media. Does libvlc flush (drop the tail)?
            a->setMedia(bPath.string());
            a->play();
        } else {
            owner.store(1);
            b->play();
        }
        std::printf("[%s] handover at +%lld ms\n", mode.c_str(), static_cast<long long>(nowMs() - t));
    }

    int run() {
        setup();
        if (mode == "s3") {
            prefetchB();
            if (!bReady.load()) {
                std::fprintf(stderr, "prefetch open failed\n");
                return 2;
            }
            std::printf("[s3] prefetched B (open only, no playback)\n");
        }
        a->open(aPath.string());
        a->play();
        const auto t0 = nowMs();
        std::uint64_t silenceAtHandover = 0;
        // Kept for the "arm on occupancy" variant that lost to the tail-drain
        // rule; the shipped trigger is `late`'s floorMs.
        const std::size_t earlyArmAt = ringFloats / 2;
        std::uint64_t renderedAtHandover = 0;
        std::uint64_t aInAtHandover = 0;
        std::uint64_t droppedAtHandover = 0;
        std::uint64_t discardedAtHandover = 0;
        bool handedOver = false;
        std::int64_t nextLog = t0;

        while (nowMs() - t0 < 20000) {
            std::this_thread::sleep_for(5ms);
            const std::uint64_t silence = sink->stats().silenceFrames.load();
            const std::uint64_t rendered = sink->stats().framesRendered.load();
            const std::uint64_t aIn = a->stats().framesIn.load();
            if (nowMs() >= nextLog) {
                nextLog = nowMs() + 500;
                std::printf("[%s] t=+%lldms aIn=%.3fs rendered=%.3fs fill=%.3fs silence=%llu "
                            "flushes=%llu discarded=%.3fs\n",
                            mode.c_str(), static_cast<long long>(nowMs() - t0),
                            static_cast<double>(aIn) / kRate,
                            static_cast<double>(rendered) / kRate,
                            static_cast<double>(sinkRing->fill() / kCh) / kRate,
                            static_cast<unsigned long long>(silence),
                            static_cast<unsigned long long>(a->stats().flushes.load()),
                            static_cast<double>(sinkRing->discarded() / kCh) / kRate);
            }
            const bool armSignal =
                mode == "early" ? (sinkRing->fill() <= earlyArmAt && a->stats().framesIn.load() > 0)
                                : endReached.load();
            if (!handedOver && armSignal) {
                handedOver = true;
                silenceAtHandover = silence;
                renderedAtHandover = rendered;
                aInAtHandover = aIn;
                droppedAtHandover = a->stats().framesDropped.load();
                discardedAtHandover = sinkRing->discarded();
                std::printf("[%s] ENDREACHED: fill=%.3fs rendered=%.3fs aIn=%.3fs silence=%llu "
                            "aDropped=%llu flushes=%llu discarded=%.3fs\n",
                            mode.c_str(), static_cast<double>(sinkRing->fill() / kCh) / kRate,
                            static_cast<double>(rendered) / kRate,
                            static_cast<double>(aIn) / kRate,
                            static_cast<unsigned long long>(silence),
                            static_cast<unsigned long long>(a->stats().framesDropped.load()),
                            static_cast<unsigned long long>(a->stats().flushes.load()),
                            static_cast<double>(sinkRing->discarded() / kCh) / kRate);
                handover();
            }
            if (pendingLate.load() &&
                static_cast<double>(sinkRing->fill() / kCh) / kRate <= floorMs / 1000.0) {
                pendingLate.store(false);
                std::printf("[late] ring drained to %.3fs; swapping media\n",
                            static_cast<double>(sinkRing->fill() / kCh) / kRate);
                a->setMedia(bPath.string());
                a->play();
            }
            // The measurement window runs to the point where every delivered
            // frame has been rendered (both tracks' PCM is fully behind us), so
            // no post-track silence leaks into the seam.
            if (handedOver && aIn >= expectedTotalFrames && sinkRing->fill() == 0) {
                break;
            }
        }

        const std::uint64_t silence = sink->stats().silenceFrames.load();
        const std::uint64_t rendered = sink->stats().framesRendered.load();
        const std::uint64_t aIn = a->stats().framesIn.load();
        const std::uint64_t seamSilence = silence - silenceAtHandover;
        const std::uint64_t seamDiscard = sinkRing->discarded() - discardedAtHandover;
        const std::uint64_t seamDrop = (a->stats().framesDropped.load() - droppedAtHandover);
        const std::uint64_t seam = seamSilence + seamDiscard / kCh + seamDrop;
        const double lostMs = static_cast<double>(RenderFrames() - aInAtHandover) / 48.0;
        (void)seamSilence;
        (void)seamDiscard;
        std::printf("[%s] SUMMARY seam=%.1fms (%llu frames) | A delivered before boundary "
                    "%.3fs of 4.000s (lost %.1fms) | B frames=%llu | aDropped=%llu flushes=%llu "
                    "underruns=%llu | rendered=%.3fs aIn=%.3fs\n",
                    mode.c_str(), static_cast<double>(seam) / 48.0,
                    static_cast<unsigned long long>(seam),
                    static_cast<double>(aInAtHandover) / kRate, lostMs,
                    static_cast<unsigned long long>(mode == "s3" ? bFrames.load() : aFrames.load()),
                    static_cast<unsigned long long>(a->stats().framesDropped.load()),
                    static_cast<unsigned long long>(a->stats().flushes.load()),
                    static_cast<unsigned long long>(sink->stats().underruns.load()),
                    static_cast<double>(rendered) / kRate,
                    static_cast<double>(aIn) / kRate);
        std::printf("[%s] discarded(ring)=%.3fs silence(total)=%.1fms\n", mode.c_str(),
                    static_cast<double>(sinkRing->discarded() / kCh) / kRate,
                    static_cast<double>(silence) / 48.0);
        (void)renderedAtHandover;
        return 0;
    }

    /// Track A is exactly 4 s; what the tap delivered of A before the boundary.
    static std::uint64_t RenderFrames() { return 4ULL * kRate; }
    static constexpr std::uint64_t expectedTotalFrames = 8ULL * kRate; // A 4 s + B 4 s
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: spike_au2 <s1|s2|late|s3> [ringFloats] [tailMs] [wav|flac]\n");
        return 2;
    }
    std::size_t ring = 48000 * 2 * 700 / 1000; // relaxed request
    if (argc >= 3) {
        ring = static_cast<std::size_t>(std::strtoul(argv[2], nullptr, 10));
    }
    Probe probe(argv[1], ring);
    if (argc >= 4) {
        probe.floorMs = std::strtod(argv[3], nullptr);
    }
    if (argc >= 5) {
        probe.container = argv[4];
    }
    return probe.run();
}
