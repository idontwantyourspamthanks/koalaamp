// SPDX-License-Identifier: MIT
//
// VisData — the classic in-window visualiser: the 1024-point spectrum
// transform, the 75-band projection with Winamp's falloff/peak decay, the 75
// point oscilloscope, and the lock-free PCM ring the window's DSP tap feeds.
//
// The transform and the band mapping are ported from Webamp (MIT)
// `js/visualizer/FFT.ts` (`timeToFrequencyDomain`) and
// `js/visualizer/Visualizer.ts` (`paintAnalyzer` / `paintOscilloscope`) at
// webamp@e6283c0eabe9b1313cd41df16d2875e897719696 — that code is itself a
// port of Winamp's `vis_fft`/`vis_analyzer`, and the numeric tables
// (bit-reversal, cosine/sine, envelope, equalise) are reproduced verbatim so
// the bars land where Winamp's do. Attribution in LICENSES.md.
//
// Constants (recorded in docs/tickets/SE-2-main-window.md):
//   * FFT size 1024, 512 magnitude bins, envelope exponent 1, equalise table
//     built from `a = 0.04; t = (9-a)/(n/2); log10(1+a+(i+1)*t); a /= 1.0025`.
//   * 75 bands: `f = log10(512) * i/74`, bin = `i/74*511*0.09 + 0.91*10^f`.
//   * "wide" bandwidth averages four consecutive samples into one band and
//     skips painting every fourth column (Winamp's default).
//   * `saFalloff` decay 12/16 = 0.75 px per frame ("moderate"), peak falloff
//     1.1 ("slow"), bars clamped to 15 (the 16 px vis area minus pushDown 2).
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ui/PlayerState.hpp"

namespace koalaamp::ui {

/// Winamp's 1024-point spectrum transform (Webamp `FFT`).
class WinampFft {
public:
    static constexpr int kSize = 1024;
    static constexpr int kBins = 512;

    WinampFft();

    /// `samples` holds `kSize` values already scaled the way Winamp's analyser
    /// scales them (`(byte - 128) / 24`); `out` receives `kBins` magnitudes.
    void transform(const float* samples, float* out) const;

private:
    std::array<int, kSize> m_bitRev{};
    std::array<std::array<float, 2>, 11> m_cosSin{};
    std::array<float, kSize> m_envelope{};
    std::array<float, kBins> m_equalize{};
    /// Scratch reused across calls (Webamp's `temp1`/`temp2`); the transform is
    /// only ever called from the GUI thread.
    mutable std::array<float, kSize> m_tmp1{};
    mutable std::array<float, kSize> m_tmp2{};
};

/// Tunables mirroring Winamp's `viscolor`-adjacent spectrum settings.
struct VisAnalyzerConfig {
    /// `saFalloff` in Winamp's units: px-per-frame decay = falloff / 16.
    int falloff = 12;          ///< "moderate"
    float peakFalloff = 1.1F;  ///< "slow"
    bool wideBandwidth = true;
    bool peaks = true;
};

/// Turns a 1024-sample mono window into a `VisFrame` (bars + peaks + osc).
class VisAnalyzer {
public:
    explicit VisAnalyzer(VisAnalyzerConfig config = {});

    /// Analyzes `count` mono samples (the most recent ones, chronological);
    /// the first `WinampFft::kSize` are used, right-aligned when short.
    void analyze(const float* samples, std::size_t count, VisFrame& frame);

    void reset();
    const VisAnalyzerConfig& config() const { return m_config; }

private:
    VisAnalyzerConfig m_config;
    WinampFft m_fft;
    std::array<std::uint8_t, WinampFft::kSize> m_bytes{};
    std::array<float, WinampFft::kSize> m_timeDomain{};
    std::array<float, WinampFft::kBins> m_spectral{};
    std::array<float, kVisBars> m_sample{};
    std::array<std::int16_t, kVisBars> m_data{};
    std::array<std::int16_t, kVisBars> m_peaks{};
    std::array<float, kVisBars> m_falloff{};
    std::array<float, kVisBars> m_data2{};
};

/// Single-producer / single-consumer float ring for the PCM tap. `push` runs on
/// the audio callback thread: it must not allocate, lock or touch the GUI
/// (docs/spec.md §2.1) — the buffer is sized at construction.
class PcmRing {
public:
    /// `capacityFrames` is rounded up to a power of two (minimum 1024).
    explicit PcmRing(std::size_t capacityFrames);

    /// Appends `frames` interleaved frames, down-mixing to mono.
    void push(const float* interleaved, std::size_t frames, int channels) noexcept;

    /// Copies up to `outSize` most recent mono samples into `out`, writing them
    /// in chronological order; returns how many were written.
    std::size_t readRecent(float* out, std::size_t outSize) const noexcept;

    void clear() noexcept;
    std::size_t capacity() const { return m_data.size(); }

private:
    std::vector<float> m_data;          ///< capacity = m_mask + 1
    std::size_t m_mask = 0;
    std::atomic<std::uint64_t> m_write{0};
};

/// Deterministic synthetic visualiser frame for offscreen renders and goldens
/// (no audio device in a test process). `step` advances the animation.
VisFrame syntheticVisFrame(int step);

}  // namespace koalaamp::ui
