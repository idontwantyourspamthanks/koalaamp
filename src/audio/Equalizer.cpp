// SPDX-License-Identifier: MIT
// audio — 10-band equalizer stage implementation. See Equalizer.hpp.

#include "audio/Equalizer.hpp"

#include <algorithm>
#include <cmath>

namespace koalaamp::audio {

namespace {
constexpr BiquadCoeffs kIdentity{};
constexpr double kGainEpsilonDb = 1e-3; ///< below this a band is a true bypass
} // namespace

Equalizer::Equalizer() {
    for (std::atomic<int>& b : m_bands) {
        b.store(kEqValueNeutral, std::memory_order_relaxed);
    }
}

bool Equalizer::prepare(int sampleRate, int channels) {
    if (sampleRate <= 0 || channels <= 0) {
        return false;
    }
    m_sampleRate = sampleRate;
    m_channels = channels;
    const std::size_t cells = static_cast<std::size_t>(channels) * kEqBands;
    m_state.assign(cells, BiquadState{});
    m_cur.assign(cells, kIdentity);
    m_target.assign(cells, kIdentity);
    m_xfadePos = 0;
    m_xfadeTotal = 0;
    m_active = false;
    m_applied = m_generation.load(std::memory_order_acquire);
    return true;
}

void Equalizer::reset() noexcept {
    for (BiquadState& s : m_state) {
        s.reset();
    }
}

void Equalizer::setSettings(const EqSettings& settings) noexcept {
    m_mode.store(static_cast<int>(settings.mode), std::memory_order_relaxed);
    m_preamp.store(settings.preamp, std::memory_order_relaxed);
    for (int b = 0; b < kEqBands; ++b) {
        m_bands[b].store(settings.bands[static_cast<std::size_t>(b)], std::memory_order_relaxed);
    }
    m_enabled.store(settings.enabled, std::memory_order_relaxed);
    m_generation.fetch_add(1, std::memory_order_release);
}

EqSettings Equalizer::loadSettings() const noexcept {
    EqSettings s;
    s.enabled = m_enabled.load(std::memory_order_relaxed);
    s.mode = static_cast<EqMode>(m_mode.load(std::memory_order_relaxed));
    s.preamp = m_preamp.load(std::memory_order_relaxed);
    for (int b = 0; b < kEqBands; ++b) {
        s.bands[static_cast<std::size_t>(b)] = m_bands[b].load(std::memory_order_relaxed);
    }
    return s;
}

EqSettings Equalizer::settings() const noexcept {
    return loadSettings();
}

void Equalizer::commitCrossfade() noexcept {
    if (m_xfadePos <= 0 || m_xfadeTotal <= 0) {
        return;
    }
    const double t =
        std::clamp(static_cast<double>(m_xfadePos) / static_cast<double>(m_xfadeTotal), 0.0, 1.0);
    for (std::size_t i = 0; i < m_cur.size(); ++i) {
        m_cur[i] = lerp(m_cur[i], m_target[i], t);
    }
    m_xfadePos = 0;
    m_xfadeTotal = 0;
}

void Equalizer::latch(int frames) noexcept {
    const std::uint32_t gen = m_generation.load(std::memory_order_acquire);
    if (gen == m_applied) {
        return;
    }
    const EqSettings s = loadSettings();

    commitCrossfade(); // freeze whatever a previous change is mid-way through

    bool active = false;
    for (int b = 0; b < kEqBands; ++b) {
        const double gainDb = eqValueToDb(s.bands[static_cast<std::size_t>(b)]);
        const double q = bandQ(s.mode, gainDb);
        const BiquadCoeffs c =
            std::abs(gainDb) < kGainEpsilonDb
                ? kIdentity
                : peakingEq(static_cast<double>(m_sampleRate),
                            kEqFrequencies[static_cast<std::size_t>(b)], q, gainDb);
        if (!(c == kIdentity)) {
            active = true;
        }
        for (int ch = 0; ch < m_channels; ++ch) {
            m_target[static_cast<std::size_t>(ch) * kEqBands + static_cast<std::size_t>(b)] = c;
        }
    }
    m_active = active;
    m_applied = gen;

    // Start a one-block coefficient crossfade from the current coefficients.
    m_xfadePos = 0;
    m_xfadeTotal = std::min(std::max(frames, 1), kEqCrossfadeFrames);
}

bool Equalizer::bypassed() const noexcept {
    return !enabled() || m_sampleRate <= 0 || m_channels <= 0 || m_state.empty() || !m_active;
}

void Equalizer::process(float* const* channels, int frames) noexcept {
    if (frames <= 0 || channels == nullptr || m_sampleRate <= 0 || m_channels <= 0 ||
        m_state.empty()) {
        return;
    }
    if (!m_enabled.load(std::memory_order_acquire)) {
        m_xfadePos = 0;
        m_xfadeTotal = 0;
        return; // bypass: state is left as-is for a later enable
    }

    latch(frames);

    if (!m_active && m_xfadePos == 0) {
        return; // flat: nothing to filter
    }

    const bool crossfading = m_xfadePos < m_xfadeTotal && m_xfadeTotal > 0;
    for (int i = 0; i < frames; ++i) {
        double p = 1.0;
        if (crossfading && m_xfadePos < m_xfadeTotal) {
            p = static_cast<double>(m_xfadePos + 1) / static_cast<double>(m_xfadeTotal);
        }
        for (int ch = 0; ch < m_channels; ++ch) {
            float* const out = channels[ch];
            const std::size_t base = static_cast<std::size_t>(ch) * kEqBands;
            for (int b = 0; b < kEqBands; ++b) {
                const std::size_t idx = base + static_cast<std::size_t>(b);
                const BiquadCoeffs c =
                    crossfading ? lerp(m_cur[idx], m_target[idx], p) : m_cur[idx];
                out[i] = m_state[idx].process(out[i], c);
            }
        }
        if (crossfading && m_xfadePos < m_xfadeTotal) {
            ++m_xfadePos;
        }
    }
    if (m_xfadeTotal > 0 && m_xfadePos >= m_xfadeTotal) {
        m_cur = m_target;
        m_xfadePos = 0;
        m_xfadeTotal = 0;
    }
}

} // namespace koalaamp::audio
