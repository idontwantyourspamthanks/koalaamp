// SPDX-License-Identifier: MIT
// audio — second-order IIR section (biquad) primitives for the DSP pipeline.
//
// Coefficient design follows the RBJ Audio-EQ-Cookbook peaking-EQ form
// (https://www.w3.org/TR/audio-eq-cookbook/ — a public reference, not a code
// copy). Coefficients and filter state are double precision; only the samples
// crossing the stage boundary are float (docs/spec.md §4.1 "double-precision
// internals, float storage").
//
// Clean-room: no WCL/Winamp source was consulted. The Webamp/winamp-eqf MIT
// reference informed the .eqf container only (see EqPreset.hpp), never this
// filter math.

#pragma once

#include <cmath>
#include <cstdint>

namespace koalaamp::audio {

/// Direct-form-I section coefficients, normalised so `a0 == 1`. `b*` are the
/// numerator taps, `a*` the (negated) feedback taps, i.e. the difference
/// equation is y[n] = b0·x[n] + b1·x[n-1] + b2·x[n-2] − a1·y[n-1] − a2·y[n-2].
struct BiquadCoeffs {
    double b0{1.0};
    double b1{0.0};
    double b2{0.0};
    double a1{0.0};
    double a2{0.0};

    friend bool operator==(const BiquadCoeffs&, const BiquadCoeffs&) = default;
};

/// Peaking-EQ coefficients (RBJ), normalised by a0. `q` must be > 0,
/// `sampleRate` > 0 and 0 < centreHz < sampleRate/2.
inline BiquadCoeffs peakingEq(double sampleRate, double centreHz, double q, double gainDb) {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * centreHz / sampleRate;
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);

    const double b0 = 1.0 + alpha * A;
    const double b1 = -2.0 * cw;
    const double b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1 = -2.0 * cw;
    const double a2 = 1.0 - alpha / A;

    return BiquadCoeffs{b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

/// Per-channel biquad *state* (Direct Form I). Coefficients are passed into
/// `process` at call time so a caller can linearly interpolate them across a
/// block for a glitch-free parameter change (ticket AU-1 notes: crossfade over
/// one block).
class BiquadState {
public:
    void reset() noexcept { m_x1 = m_x2 = m_y1 = m_y2 = 0.0; }

    /// One sample, float in/out, double state.
    float process(float x, const BiquadCoeffs& c) noexcept {
        const double xd = static_cast<double>(x);
        const double y = c.b0 * xd + c.b1 * m_x1 + c.b2 * m_x2 - c.a1 * m_y1 - c.a2 * m_y2;
        m_x2 = m_x1;
        m_x1 = xd;
        m_y2 = m_y1;
        m_y1 = y;
        return static_cast<float>(y);
    }

private:
    double m_x1{0.0};
    double m_x2{0.0};
    double m_y1{0.0};
    double m_y2{0.0};
};

/// Component-wise linear blend of two coefficient sets (`t` in [0, 1]).
inline BiquadCoeffs lerp(const BiquadCoeffs& a, const BiquadCoeffs& b, double t) noexcept {
    return BiquadCoeffs{
        a.b0 + (b.b0 - a.b0) * t, a.b1 + (b.b1 - a.b1) * t, a.b2 + (b.b2 - a.b2) * t,
        a.a1 + (b.a1 - a.a1) * t, a.a2 + (b.a2 - a.a2) * t,
    };
}

} // namespace koalaamp::audio
