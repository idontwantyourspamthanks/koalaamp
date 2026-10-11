// SPDX-License-Identifier: MIT
// audio — 10-band peaking-biquad equalizer stage (docs/spec.md §4.2).
//
// One peaking biquad per band per channel, coefficients derived in double from
// the Winamp band table and the selected curve mode (EqValue.hpp). Settings
// arrive from the control thread while `process` runs on the audio-output
// thread, so the hand-off is a set of atomics plus a generation counter; the
// stage latches them at a block boundary and *crossfades the coefficients over
// one block* so a preset/slider change stays glitch-free (ticket AU-1 note;
// SE-5's acceptance).
//
// The stage is bypassed (zero cost) when disabled or when every band is at
// unity gain.

#pragma once

#include "audio/Biquad.hpp"
#include "audio/EqPreset.hpp"
#include "audio/EqValue.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

namespace koalaamp::audio {

/// Master EQ settings (the 0-64 slider values included). `preamp` is applied
/// by DspPipeline as its own gain stage *after* the biquads (docs/spec.md §4.1),
/// so the Equalizer itself only filters.
struct EqSettings {
    bool enabled{false};
    EqMode mode{EqMode::WinampCurve};
    int preamp{kEqValueNeutral};
    std::array<int, kEqBands> bands{
        kEqValueNeutral, kEqValueNeutral, kEqValueNeutral, kEqValueNeutral, kEqValueNeutral,
        kEqValueNeutral, kEqValueNeutral, kEqValueNeutral, kEqValueNeutral, kEqValueNeutral};
};

/// Coefficient crossfade length in frames (one 1024-frame block, per the ticket).
inline constexpr int kEqCrossfadeFrames = 1024;

/// Build stage settings from a stored preset (used by the auto-load path).
inline EqSettings eqSettingsFromPreset(const EqPreset& preset, bool enabled = true,
                                       EqMode mode = EqMode::WinampCurve) {
    EqSettings s;
    s.enabled = enabled;
    s.mode = mode;
    s.preamp = preset.preamp;
    s.bands = preset.bands;
    return s;
}

/// Snapshot stage settings into a preset (for "save current EQ").
inline EqPreset presetFromEqSettings(const EqSettings& settings, std::string name = {}) {
    EqPreset p;
    p.name = std::move(name);
    p.bands = settings.bands;
    p.preamp = settings.preamp;
    return p;
}

class Equalizer {
public:
    Equalizer();
    Equalizer(const Equalizer&) = delete;
    Equalizer& operator=(const Equalizer&) = delete;

    /// Allocate/reset for a format. Any change of rate/channels reallocates
    /// (call from the setup path, not the audio thread). Returns false if the
    /// format is unusable.
    bool prepare(int sampleRate, int channels);

    void reset() noexcept;

    /// Control-thread settings push; latched by the audio thread at the next
    /// block boundary. Lock-free, allocation-free.
    void setSettings(const EqSettings& settings) noexcept;
    EqSettings settings() const noexcept;

    bool enabled() const noexcept { return m_enabled.load(std::memory_order_acquire); }

    /// Planar processing: `channels[c][0..frames)` is one channel's block.
    /// `channels` must have `channels()` valid pointers and `frames` may be any
    /// length (the crossfade spans at most one 1024-frame block).
    void process(float* const* channels, int frames) noexcept;

    /// True when there is nothing to do (disabled, unprepared, or flat).
    bool bypassed() const noexcept;

    int sampleRate() const noexcept { return m_sampleRate; }
    int channels() const noexcept { return m_channels; }

private:
    EqSettings loadSettings() const noexcept; ///< atomic snapshot
    void latch(int frames) noexcept;          ///< generation check → targets + crossfade
    void commitCrossfade() noexcept;

    // Control-thread written, audio-thread read.
    std::atomic<bool> m_enabled{false};
    std::atomic<int> m_mode{static_cast<int>(EqMode::WinampCurve)};
    std::atomic<int> m_preamp{kEqValueNeutral};
    std::atomic<int> m_bands[kEqBands];
    std::atomic<std::uint32_t> m_generation{0};

    // Audio-thread state.
    std::uint32_t m_applied{0};
    int m_sampleRate{0};
    int m_channels{0};
    std::vector<BiquadState> m_state; ///< channels * kEqBands
    std::vector<BiquadCoeffs> m_cur;  ///< current (possibly mid-crossfade)
    std::vector<BiquadCoeffs> m_target;
    int m_xfadePos{0};
    int m_xfadeTotal{0};
    bool m_active{false};
};

} // namespace koalaamp::audio
