// SPDX-License-Identifier: MIT
// audio — soft limiter / clipping-prevention stage (docs/spec.md §4.1).
//
// Winamp's "limiter" flag is a clipping-prevention toggle on the EQ. This stage
// is a memoryless soft-knee compressor: transparent below its threshold, a
// 6 dB soft knee centred on −0.1 dBFS, a 1.5:1 slope above the knee, and a hard
// 0 dBFS output ceiling so a boosted preset can never wrap past full scale.
// Memoryless (no attack/release envelope) keeps it deterministic and testable;
// the ticket only pins the knee/ratio/ceiling numbers.

#pragma once

#include <cmath>

namespace koalaamp::audio {

class Limiter {
public:
    /// Threshold at the knee centre: −0.1 dBFS.
    static constexpr double kThresholdDb = -0.1;
    /// Compression slope above the knee (1.5:1).
    static constexpr double kRatio = 1.5;
    /// Total soft-knee width (dB) about the threshold.
    static constexpr double kKneeDb = 6.0;
    /// Output ceiling: 0 dBFS (linear 1.0).
    static constexpr double kCeilingDb = 0.0;

    void setEnabled(bool enabled) noexcept { m_enabled = enabled; }
    bool enabled() const noexcept { return m_enabled; }

    /// Static gain-reduction curve. `x` is a linear sample in [-1, 1] (or
    /// beyond, pre-limiter); the return value is bounded by the ceiling.
    float process(float x) const noexcept {
        if (!m_enabled) {
            return x;
        }
        const double a = std::fabs(static_cast<double>(x));
        if (a < 1e-20) {
            return x;
        }
        const double levelDb = 20.0 * std::log10(a);
        const double half = kKneeDb / 2.0;
        const double kneeStart = kThresholdDb - half;
        double outDb = levelDb;
        if (levelDb >= kThresholdDb + half) {
            outDb = kThresholdDb + (levelDb - kThresholdDb) / kRatio;
        } else if (levelDb > kneeStart) {
            const double over = levelDb - kneeStart;
            outDb = levelDb + (1.0 / kRatio - 1.0) * over * over / (2.0 * kKneeDb);
        }
        if (outDb > kCeilingDb) {
            outDb = kCeilingDb;
        }
        const double out = std::pow(10.0, outDb / 20.0);
        return static_cast<float>(x < 0.0f ? -out : out);
    }

private:
    bool m_enabled{false};
};

} // namespace koalaamp::audio
