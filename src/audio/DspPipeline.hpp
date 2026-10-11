// SPDX-License-Identifier: MIT
// audio — the DSP transform installed at the PlaybackCore seam (AU-1).
//
// Chain (docs/spec.md §4.1, docs/tickets/AU-1-dsp-eq.md Task 1):
//   interleaved float in
//     → deinterleave
//     → ReplayGain (album/track, preamp, peak-aware clip prevention, −6 dB
//       fallback)
//     → 10× peaking biquads (Winamp table; Winamp-curve or Constant-Q)
//     → master preamp
//     → soft limiter
//     → interleave
//     → same buffer out
//
// It is a *transform*, not a sink: PlaybackCore still owns tap → SPSC ring →
// miniaudio sink, and the transform is installed with
// `IAudioEngine::setPcmCallback` (see `makeCallback`). The block is processed
// in place, in ≤1024-frame chunks, with pre-allocated buffers — steady-state
// heap traffic is zero.
//
// Threading: `process` runs on libvlc's audio-output thread. Settings and
// ReplayGain tags are pushed from the control thread into atomics and latched
// at a block boundary; no lock, no allocation, no GUI call happens on the audio
// thread.

#pragma once

#include "audio/Equalizer.hpp"
#include "audio/Limiter.hpp"
#include "audio/ReplayGain.hpp"
#include "playback/IAudioEngine.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace koalaamp::audio {

struct DspSettings {
    bool enabled{false}; ///< whole stage; false = bit-perfect-ish passthrough
    EqSettings eq;
    RgConfig rg;
    bool limiterEnabled{false};
};

class DspPipeline {
public:
    DspPipeline();
    DspPipeline(const DspPipeline&) = delete;
    DspPipeline& operator=(const DspPipeline&) = delete;

    /// Allocate the working buffers and prepare the stages. Call from the setup
    /// path; `process` calls it lazily if the format changes.
    bool prepare(int sampleRate, int channels);

    void setSettings(const DspSettings& settings) noexcept;
    DspSettings settings() const noexcept;

    /// ReplayGain tags for the current track (control thread).
    void setReplayGain(const RgInfo& info) noexcept;

    /// In-place transform of one interleaved block. Allocation-free in steady
    /// state; any block length is accepted (internally chunked to 1024 frames).
    void process(float* interleaved, std::size_t frames, int sampleRate, int channels) noexcept;

    /// The seam adapter. Returns an `IAudioEngine::PcmCb` that transforms the
    /// block in place and then forwards the *post-DSP* samples to `downstream`
    /// (the visualiser/spectrum tap, docs/spec.md §3.1). Install it with
    /// `PlaybackCore::setPcmCallback`.
    playback::IAudioEngine::PcmCb makeCallback(playback::IAudioEngine::PcmCb downstream = {});

    /// Added buffering latency of the pipeline in frames. The stage is a
    /// per-block in-place transform with no lookahead, so this is 0 (≤10 ms
    /// over the engine's passthrough is trivially met; FE-2's decode-block
    /// floor is unchanged).
    static constexpr std::size_t addedLatencyFrames() noexcept { return 0; }

    struct Stats {
        std::uint64_t blocks{0};
        std::uint64_t frames{0};
        std::uint64_t processedBlocks{0}; ///< blocks on which the chain did work
        double gainDb{0.0};               ///< ReplayGain gain currently applied
        bool rgFallback{false};
        bool rgClipLimited{false};
    };
    Stats stats() const noexcept;

    Equalizer& equalizer() noexcept { return m_eq; }

private:
    static constexpr int kChunkFrames = 1024;

    void prepareBuffersLocked(int sampleRate, int channels);
    void latch() noexcept; ///< pick up control-thread changes at a block boundary

    // Control-thread written, audio-thread read.
    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_limiterEnabled{false};
    std::atomic<int> m_rgMode{static_cast<int>(RgMode::Off)};
    std::atomic<double> m_rgPreampDb{0.0};
    std::atomic<bool> m_rgPreventClip{true};
    std::atomic<double> m_rgFallbackDb{-6.0};
    // Tag values; NaN == absent.
    std::atomic<double> m_trackGain{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<double> m_trackPeak{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<double> m_albumGain{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<double> m_albumPeak{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<double> m_trackLufs{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<double> m_albumLufs{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<std::uint32_t> m_generation{0};

    // Audio-thread state.
    std::uint32_t m_applied{0};
    int m_sampleRate{0};
    int m_channels{0};
    std::vector<float> m_planar;  ///< channels * kChunkFrames
    std::vector<float*> m_chPtrs; ///< per-channel views into m_planar
    double m_rgLinear{1.0};
    double m_preampLinear{1.0};
    RgResult m_rgResult;
    Equalizer m_eq;
    Limiter m_limiter;

    // Readable from any thread while the audio thread runs.
    std::atomic<std::uint64_t> m_blocks{0};
    std::atomic<std::uint64_t> m_frames{0};
    std::atomic<std::uint64_t> m_processedBlocks{0};
    std::atomic<double> m_appliedGainDb{0.0};
    std::atomic<bool> m_rgFallbackFlag{false};
    std::atomic<bool> m_rgClipLimitedFlag{false};
};

} // namespace koalaamp::audio
