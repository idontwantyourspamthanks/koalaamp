// SPDX-License-Identifier: MIT
// audio — DspPipeline implementation. See DspPipeline.hpp.

#include "audio/DspPipeline.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace koalaamp::audio {

namespace {

using playback::IAudioEngine;
using playback::PcmFormat;

/// The engine hands the seam its mutable pre-ring scratch through a read-only
/// view; the insertion seam exists precisely so a transform may rewrite that
/// block (docs/spec.md §3.1, AU-1 Task 1). The underlying object is not const,
/// so casting away the view's constness and writing is well-defined.
float* mutableBlock(const float* interleaved) noexcept {
    return const_cast<float*>(interleaved);
}

std::optional<double> fromAtomic(const std::atomic<double>& value) noexcept {
    const double v = value.load(std::memory_order_relaxed);
    return std::isnan(v) ? std::nullopt : std::optional<double>(v);
}

} // namespace

DspPipeline::DspPipeline() = default;

bool DspPipeline::prepare(int sampleRate, int channels) {
    if (sampleRate <= 0 || channels <= 0) {
        return false;
    }
    // Do NOT reset m_applied to the current generation here: settings pushed
    // before the format is established (app startup's DspSettings, track RG
    // info before the first block) must survive into the first process()
    // call, whose latch() then applies them. Marking them applied without
    // latching silently dropped the whole startup configuration (EQ and RG
    // stayed unity until a later slider move bumped the generation).
    prepareBuffersLocked(sampleRate, channels);
    // NOTE (AU-2, same fix found independently on both branches): deliberately
    // does not advance `m_applied`. Settings and ReplayGain tags are pushed
    // *before* a track's first block (the core's prepare hook runs before play),
    // and the first process() call is also what lazily calls prepare() for a new
    // format — marking the generation applied here would swallow exactly those
    // pending changes, leaving the track untagged (measured: gain stuck at 0 dB
    // / the app's startup DSP inert until a slider move). Leaving `m_applied`
    // stale makes the next block latch whatever the control thread last pushed.
    return true;
}

void DspPipeline::prepareBuffersLocked(int sampleRate, int channels) {
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_planar.assign(static_cast<std::size_t>(channels) * kChunkFrames, 0.0f);
    m_chPtrs.resize(static_cast<std::size_t>(channels));
    for (int c = 0; c < channels; ++c) {
        m_chPtrs[static_cast<std::size_t>(c)] =
            m_planar.data() + static_cast<std::size_t>(c) * kChunkFrames;
    }
    m_eq.prepare(sampleRate, channels);
}

void DspPipeline::setSettings(const DspSettings& settings) noexcept {
    m_enabled.store(settings.enabled, std::memory_order_relaxed);
    m_limiterEnabled.store(settings.limiterEnabled, std::memory_order_relaxed);
    m_rgMode.store(static_cast<int>(settings.rg.mode), std::memory_order_relaxed);
    m_rgPreampDb.store(settings.rg.preampDb, std::memory_order_relaxed);
    m_rgPreventClip.store(settings.rg.preventClip, std::memory_order_relaxed);
    m_rgFallbackDb.store(settings.rg.fallbackDb, std::memory_order_relaxed);
    m_eq.setSettings(settings.eq);
    m_generation.fetch_add(1, std::memory_order_release);
}

DspSettings DspPipeline::settings() const noexcept {
    DspSettings s;
    s.enabled = m_enabled.load(std::memory_order_relaxed);
    s.limiterEnabled = m_limiterEnabled.load(std::memory_order_relaxed);
    s.rg.mode = static_cast<RgMode>(m_rgMode.load(std::memory_order_relaxed));
    s.rg.preampDb = m_rgPreampDb.load(std::memory_order_relaxed);
    s.rg.preventClip = m_rgPreventClip.load(std::memory_order_relaxed);
    s.rg.fallbackDb = m_rgFallbackDb.load(std::memory_order_relaxed);
    s.eq = m_eq.settings();
    return s;
}

void DspPipeline::setReplayGain(const RgInfo& info) noexcept {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    m_trackGain.store(info.trackGainDb.value_or(nan), std::memory_order_relaxed);
    m_trackPeak.store(info.trackPeak.value_or(nan), std::memory_order_relaxed);
    m_albumGain.store(info.albumGainDb.value_or(nan), std::memory_order_relaxed);
    m_albumPeak.store(info.albumPeak.value_or(nan), std::memory_order_relaxed);
    m_trackLufs.store(info.trackLufs.value_or(nan), std::memory_order_relaxed);
    m_albumLufs.store(info.albumLufs.value_or(nan), std::memory_order_relaxed);
    m_generation.fetch_add(1, std::memory_order_release);
}

DspPipeline::Stats DspPipeline::stats() const noexcept {
    Stats s;
    s.blocks = m_blocks.load(std::memory_order_relaxed);
    s.frames = m_frames.load(std::memory_order_relaxed);
    s.processedBlocks = m_processedBlocks.load(std::memory_order_relaxed);
    s.gainDb = m_appliedGainDb.load(std::memory_order_relaxed);
    s.rgFallback = m_rgFallbackFlag.load(std::memory_order_relaxed);
    s.rgClipLimited = m_rgClipLimitedFlag.load(std::memory_order_relaxed);
    return s;
}

void DspPipeline::latch() noexcept {
    const std::uint32_t gen = m_generation.load(std::memory_order_acquire);
    if (gen == m_applied) {
        return;
    }

    DspSettings s = settings();
    m_eq.setSettings(s.eq);

    RgInfo info;
    info.trackGainDb = fromAtomic(m_trackGain);
    info.trackPeak = fromAtomic(m_trackPeak);
    info.albumGainDb = fromAtomic(m_albumGain);
    info.albumPeak = fromAtomic(m_albumPeak);
    info.trackLufs = fromAtomic(m_trackLufs);
    info.albumLufs = fromAtomic(m_albumLufs);

    m_rgResult = computeReplayGain(info, s.rg);
    m_rgLinear = m_rgResult.linearGain;
    m_preampLinear = std::pow(10.0, eqValueToDb(s.eq.preamp) / 20.0);
    m_limiter.setEnabled(s.limiterEnabled);

    m_appliedGainDb.store(m_rgResult.gainDb, std::memory_order_relaxed);
    m_rgFallbackFlag.store(m_rgResult.fallback, std::memory_order_relaxed);
    m_rgClipLimitedFlag.store(m_rgResult.clipLimited, std::memory_order_relaxed);

    m_applied = gen;
}

void DspPipeline::process(float* interleaved, std::size_t frames, int sampleRate,
                          int channels) noexcept {
    if (interleaved == nullptr || frames == 0 || channels <= 0) {
        return;
    }
    if (sampleRate != m_sampleRate || channels != m_channels || m_chPtrs.empty() ||
        static_cast<std::size_t>(channels) != m_chPtrs.size()) {
        if (!prepare(sampleRate, channels)) {
            return;
        }
    }

    m_blocks.fetch_add(1, std::memory_order_relaxed);
    m_frames.fetch_add(frames, std::memory_order_relaxed);

    if (!m_enabled.load(std::memory_order_acquire)) {
        return; // passthrough
    }

    latch();

    // Per-stage activity: a transparent stage is skipped inside the loop, but
    // the block still passes through the deinterleave/interleave step so the
    // transform is a no-op rather than a partial write.
    const bool rgActive = m_rgLinear != 1.0;
    const bool preampActive = m_preampLinear != 1.0;
    const bool limiterActive = m_limiter.enabled();

    m_processedBlocks.fetch_add(1, std::memory_order_relaxed);

    const int ch = m_channels;
    std::size_t offset = 0;
    while (offset < frames) {
        const int n = static_cast<int>(std::min<std::size_t>(frames - offset, kChunkFrames));

        // interleaved → planar
        for (int c = 0; c < ch; ++c) {
            float* dst = m_chPtrs[static_cast<std::size_t>(c)];
            for (int i = 0; i < n; ++i) {
                dst[i] = interleaved[(offset + static_cast<std::size_t>(i)) *
                                         static_cast<std::size_t>(ch) +
                                     static_cast<std::size_t>(c)];
            }
        }

        if (rgActive) {
            const float gain = static_cast<float>(m_rgLinear);
            for (int c = 0; c < ch; ++c) {
                float* x = m_chPtrs[static_cast<std::size_t>(c)];
                for (int i = 0; i < n; ++i) {
                    x[i] *= gain;
                }
            }
        }

        m_eq.process(m_chPtrs.data(), n);

        if (preampActive) {
            const float gain = static_cast<float>(m_preampLinear);
            for (int c = 0; c < ch; ++c) {
                float* x = m_chPtrs[static_cast<std::size_t>(c)];
                for (int i = 0; i < n; ++i) {
                    x[i] *= gain;
                }
            }
        }

        if (limiterActive) {
            for (int c = 0; c < ch; ++c) {
                float* x = m_chPtrs[static_cast<std::size_t>(c)];
                for (int i = 0; i < n; ++i) {
                    x[i] = m_limiter.process(x[i]);
                }
            }
        }

        // planar → interleaved
        for (int c = 0; c < ch; ++c) {
            const float* src = m_chPtrs[static_cast<std::size_t>(c)];
            for (int i = 0; i < n; ++i) {
                interleaved[(offset + static_cast<std::size_t>(i)) * static_cast<std::size_t>(ch) +
                            static_cast<std::size_t>(c)] = src[i];
            }
        }

        offset += static_cast<std::size_t>(n);
    }
}

IAudioEngine::PcmCb DspPipeline::makeCallback(IAudioEngine::PcmCb downstream) {
    return [this, downstream = std::move(downstream)](const float* interleaved, std::size_t frames,
                                                      const PcmFormat& format) {
        process(mutableBlock(interleaved), frames, format.sampleRate, format.channels);
        if (downstream) {
            downstream(interleaved, frames, format);
        }
    };
}

} // namespace koalaamp::audio
