// SPDX-License-Identifier: MIT
// interop/vlc — FE-2 measurement harness.
//
// Plays one or more local media files through the libvlc callback path
// (VlcPcmTap) into a miniaudio sink and reports, per run: callback period,
// decode-ahead backlog latency (p95/max), sink period, underrun/overrun
// counts and process CPU. Optionally measures the stop()→new-media→first-PCM
// seam on both the callback path and the native aout path.
//
// Usage:
//   koalaamp_vlc_spike [--backend alsa|pulse|auto] [--file PATH]... [--duration S]
//                      [--ring-ms N] [--period N] [--label NAME] [--json PATH]
//                      [--seam] [--native-seam [--aout NAME]] [--quiet]
//
// This is a spike tool, not shipped UI: no DSP, straight PCM pass-through.

#include "interop/vlc/MiniAudioSink.hpp"
#include "interop/vlc/VlcPcmTap.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <vlc/vlc.h>

namespace {

using namespace std::chrono_literals;
using koalaamp::interop::vlc::FloatRing;
using koalaamp::interop::vlc::MiniAudioSink;
using koalaamp::interop::vlc::SinkBackend;
using koalaamp::interop::vlc::SinkConfig;
using koalaamp::interop::vlc::VlcPcmTap;

std::int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

double cpuSeconds() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
    auto toSeconds = [](const timeval& tv) {
        return static_cast<double>(tv.tv_sec) + static_cast<double>(tv.tv_usec) / 1e6;
    };
    return toSeconds(ru.ru_utime) + toSeconds(ru.ru_stime);
}

void writeSineWav(const std::string& path, double hz, unsigned rate, unsigned channels,
                  unsigned seconds) {
    std::vector<std::int16_t> pcm;
    pcm.reserve(static_cast<std::size_t>(rate) * seconds * channels);
    for (unsigned i = 0; i < rate * seconds; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(rate);
        const auto v =
            static_cast<std::int16_t>(std::lround(0.5 * 32767.0 * std::sin(2.0 * M_PI * hz * t)));
        for (unsigned c = 0; c < channels; ++c) {
            pcm.push_back(v);
        }
    }
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * 2);
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4);
    u32(36 + dataBytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    u32(16);
    u16(1); // PCM
    u16(static_cast<std::uint16_t>(channels));
    u32(rate);
    u32(rate * channels * 2);
    u16(static_cast<std::uint16_t>(channels * 2));
    u16(16);
    f.write("data", 4);
    u32(dataBytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
}

struct Counters {
    std::uint64_t callbacks{0}, framesIn{0}, framesDropped{0};
    std::uint64_t deviceCallbacks{0}, underruns{0}, silenceFrames{0};
};

/// Callback-path seam: stop() → set new media → play → first decoded PCM.
double seamCallback(VlcPcmTap& tap, const std::string& a, const std::string& b) {
    tap.setMedia(a);
    tap.play();
    std::this_thread::sleep_for(1500ms);
    const std::int64_t t0 = nowNs();
    tap.stop();
    tap.setMedia(b);
    tap.armFirstCallback();
    tap.play();
    const std::int64_t deadline = nowNs() + 5'000'000'000LL;
    while (tap.stats().firstCallbackNs.load() == 0 && nowNs() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    const std::int64_t t1 = tap.stats().firstCallbackNs.load();
    tap.stop();
    return t1 != 0 ? static_cast<double>(t1 - t0) / 1e6 : -1.0;
}

/// Native-aout seam: same sequence but libvlc owns the output device. The seam
/// is the stop()→new-media→`MediaPlayerPlaying` transition (VLC exposes no PCM
/// hook on the native path, so this is a state-transition proxy; the callback
/// figure is the meaningful one).
double seamNative(const std::string& a, const std::string& b, const std::string& aout) {
    std::vector<const char*> args{"--quiet", "--no-video", "--no-spu", "--no-osd"};
    std::string aoutArg;
    if (!aout.empty()) {
        aoutArg = "--aout=" + aout;
        args.push_back(aoutArg.c_str());
    }
    libvlc_instance_t* inst = libvlc_new(static_cast<int>(args.size()), args.data());
    if (!inst) {
        return -1.0;
    }
    libvlc_media_player_t* mp = libvlc_media_player_new(inst);
    struct Ctx {
        std::atomic<bool> playing{false};
    } ctx;
    auto onEvent = [](const libvlc_event_t*, void* data) {
        static_cast<Ctx*>(data)->playing.store(true, std::memory_order_release);
    };
    libvlc_event_manager_t* em = libvlc_media_player_event_manager(mp);
    libvlc_event_attach(em, libvlc_MediaPlayerPlaying, onEvent, &ctx);

    auto setMedia = [&](const std::string& p) {
        libvlc_media_t* m = libvlc_media_new_path(inst, p.c_str());
        libvlc_media_player_set_media(mp, m);
        libvlc_media_release(m);
    };
    auto waitPlaying = [&](std::int64_t timeoutMs) {
        const std::int64_t deadline = nowNs() + timeoutMs * 1'000'000LL;
        while (!ctx.playing.load(std::memory_order_acquire) && nowNs() < deadline) {
            std::this_thread::sleep_for(1ms);
        }
        return ctx.playing.load(std::memory_order_acquire);
    };

    setMedia(a);
    libvlc_media_player_play(mp);
    waitPlaying(5000);
    std::this_thread::sleep_for(1500ms);

    ctx.playing.store(false, std::memory_order_release);
    const std::int64_t t0 = nowNs();
    libvlc_media_player_stop(mp);
    setMedia(b);
    libvlc_media_player_play(mp);
    const bool ok = waitPlaying(5000);
    const std::int64_t t1 = nowNs();

    libvlc_event_detach(em, libvlc_MediaPlayerPlaying, onEvent, &ctx);
    libvlc_media_player_stop(mp);
    libvlc_media_player_release(mp);
    libvlc_release(inst);
    return ok ? static_cast<double>(t1 - t0) / 1e6 : -1.0;
}

} // namespace

int main(int argc, char** argv) {
    SinkBackend backend = SinkBackend::Auto;
    std::vector<std::string> files;
    double duration = 10.0;
    double ringMs = 150.0;
    unsigned period = 512;
    std::string label = "run";
    std::string jsonPath;
    std::string nativeAout = "pulse";
    bool doSeam = false;
    bool doNativeSeam = false;
    bool quiet = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--backend") {
            const std::string v = next();
            backend = v == "alsa"    ? SinkBackend::Alsa
                      : v == "pulse" ? SinkBackend::Pulse
                                     : SinkBackend::Auto;
        } else if (a == "--file") {
            files.push_back(next());
        } else if (a == "--duration") {
            duration = std::stod(next());
        } else if (a == "--ring-ms") {
            ringMs = std::stod(next());
        } else if (a == "--period") {
            period = static_cast<unsigned>(std::stoul(next()));
        } else if (a == "--label") {
            label = next();
        } else if (a == "--json") {
            jsonPath = next();
        } else if (a == "--aout") {
            nativeAout = next();
        } else if (a == "--seam") {
            doSeam = true;
        } else if (a == "--native-seam") {
            doNativeSeam = true;
        } else if (a == "--quiet") {
            quiet = true;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            return 2;
        }
    }

    if (files.empty()) {
        const std::string gen = "/tmp/koalaamp_fe2_tone.wav";
        writeSineWav(gen, 1000.0, 48000, 2, 5);
        files.push_back(gen);
        if (!quiet) {
            std::fprintf(stderr, "no --file given; generated %s\n", gen.c_str());
        }
    }

    const unsigned channels = 2;
    const unsigned sampleRate = 48000;
    const std::size_t ringFloats =
        static_cast<std::size_t>(ringMs * sampleRate / 1000.0 * channels);

    // --- latency / xrun measurement ----------------------------------------
    VlcPcmTap tap(ringFloats);
    tap.enableTiming(1u << 21);
    if (!tap.open(files.front())) {
        std::fprintf(stderr, "failed to open %s\n", files.front().c_str());
        return 1;
    }

    SinkConfig cfg;
    cfg.sampleRate = sampleRate;
    cfg.channels = channels;
    cfg.periodFrames = period;
    cfg.backend = backend;
    MiniAudioSink sink(tap.ring()); // one ring: the tap produces, the sink consumes
    if (!sink.start(cfg)) {
        std::fprintf(stderr, "failed to start miniaudio sink (backend %d)\n",
                     static_cast<int>(backend));
        return 1;
    }

    tap.play();

    // Warm-up: let the decoder fill the ring and the device settle.
    std::this_thread::sleep_for(2s);
    Counters base;
    base.callbacks = tap.stats().callbacks.load();
    base.framesIn = tap.stats().framesIn.load();
    base.framesDropped = tap.stats().framesDropped.load();
    base.deviceCallbacks = sink.stats().deviceCallbacks.load();
    base.underruns = sink.stats().underruns.load();
    base.silenceFrames = sink.stats().silenceFrames.load();

    const double cpu0 = cpuSeconds();
    const std::int64_t wall0 = nowNs();
    const auto stopAt = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(static_cast<std::int64_t>(duration * 1000));
    int ticks = 0;
    while (std::chrono::steady_clock::now() < stopAt) {
        std::this_thread::sleep_for(1s);
        if (!quiet && (++ticks % 5) == 0) {
            std::fprintf(
                stderr, "[%s] %ds: callbacks=%llu frames_in=%llu underruns=%llu drops=%llu\n",
                label.c_str(), ticks,
                static_cast<unsigned long long>(tap.stats().callbacks.load() - base.callbacks),
                static_cast<unsigned long long>(tap.stats().framesIn.load() - base.framesIn),
                static_cast<unsigned long long>(sink.stats().underruns.load() - base.underruns),
                static_cast<unsigned long long>(tap.stats().framesDropped.load() -
                                                base.framesDropped));
        }
    }
    const std::int64_t wall1 = nowNs();
    const double cpu1 = cpuSeconds();

    const Counters end{tap.stats().callbacks.load(),     tap.stats().framesIn.load(),
                       tap.stats().framesDropped.load(), sink.stats().deviceCallbacks.load(),
                       sink.stats().underruns.load(),    sink.stats().silenceFrames.load()};

    const double wallS = static_cast<double>(wall1 - wall0) / 1e9;
    const double cpuPct = wallS > 0 ? (cpu1 - cpu0) / wallS * 100.0 : 0.0;
    const double periodMs = sink.periodFrames() * 1000.0 / sampleRate;
    const double backlogP95 = sink.backlogP95Ms();
    const double residencyP95 = sink.residencyP95Ms();
    const double totalP95 = sink.latencyP95Ms();

    double seamCb = -1.0;
    double seamNat = -1.0;
    if (doSeam) {
        seamCb = seamCallback(tap, files.front(), files.size() > 1 ? files[1] : files.front());
    }
    if (doNativeSeam) {
        seamNat =
            seamNative(files.front(), files.size() > 1 ? files[1] : files.front(), nativeAout);
    }

    tap.stop();
    sink.stop();

    if (!quiet) {
        std::printf("%-10s backend=%-10s period=%.2fms residency_p95=%.2fms backlog_p95=%.2fms "
                    "total_p95=%.2fms underruns=%llu overruns_frames=%llu cpu=%.1f%%\n",
                    label.c_str(), sink.backendName().c_str(), periodMs, residencyP95, backlogP95,
                    totalP95, static_cast<unsigned long long>(end.underruns - base.underruns),
                    static_cast<unsigned long long>(end.framesDropped - base.framesDropped),
                    cpuPct);
        if (doSeam) {
            std::printf("%-10s seam(callback)=%.2fms\n", label.c_str(), seamCb);
        }
        if (doNativeSeam) {
            std::printf("%-10s seam(native aout=%s)=%.2fms\n", label.c_str(), nativeAout.c_str(),
                        seamNat);
        }
    }

    if (!jsonPath.empty()) {
        std::ofstream j(jsonPath);
        j << "{\n"
          << "  \"label\": \"" << label << "\",\n"
          << "  \"backend\": \"" << sink.backendName() << "\",\n"
          << "  \"sample_rate\": " << sampleRate << ",\n"
          << "  \"channels\": " << channels << ",\n"
          << "  \"ring_floats\": " << ringFloats << ",\n"
          << "  \"ring_ms\": " << ringMs << ",\n"
          << "  \"period_frames\": " << sink.periodFrames() << ",\n"
          << "  \"period_ms\": " << periodMs << ",\n"
          << "  \"duration_s\": " << duration << ",\n"
          << "  \"wall_s\": " << wallS << ",\n"
          << "  \"cpu_percent\": " << cpuPct << ",\n"
          << "  \"callbacks\": " << (end.callbacks - base.callbacks) << ",\n"
          << "  \"frames_in\": " << (end.framesIn - base.framesIn) << ",\n"
          << "  \"frames_dropped\": " << (end.framesDropped - base.framesDropped) << ",\n"
          << "  \"device_callbacks\": " << (end.deviceCallbacks - base.deviceCallbacks) << ",\n"
          << "  \"underruns\": " << (end.underruns - base.underruns) << ",\n"
          << "  \"silence_frames\": " << (end.silenceFrames - base.silenceFrames) << ",\n"
          << "  \"cb_interval_p50_ms\": " << tap.callbackIntervalP50ms() << ",\n"
          << "  \"cb_interval_p95_ms\": " << tap.callbackIntervalP95ms() << ",\n"
          << "  \"backlog_p95_ms\": " << backlogP95 << ",\n"
          << "  \"backlog_max_ms\": " << sink.backlogMaxMs() << ",\n"
          << "  \"residency_p95_ms\": " << residencyP95 << ",\n"
          << "  \"latency_p95_ms\": " << totalP95 << ",\n"
          << "  \"seam_callback_ms\": " << seamCb << ",\n"
          << "  \"seam_native_ms\": " << seamNat << "\n"
          << "}\n";
    }
    return 0;
}
