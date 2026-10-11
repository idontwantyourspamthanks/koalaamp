// SPDX-License-Identifier: MIT
// audio — Winamp EQ scale, band table and curve modes (docs/spec.md §4.2).
//
// The UI/serialised scale is Winamp's 6-bit slider field: 0..63 where 0 is
// full boost (+12 dB), 31 is unity (0 dB) and 63 is full cut (-12 dB). That
// ordering (and 31 == 0 dB) is what the .eqf/.q1 byte layout stores; it was
// confirmed against the published Winamp preset files (see EqPreset.hpp).
//
// Task curtain: AU-1 owns the mapping SE-5's sliders consume.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace koalaamp::audio {

inline constexpr int kEqBands = 10;

inline constexpr int kEqValueMin = 0;      ///< +12 dB
inline constexpr int kEqValueNeutral = 31; ///< 0 dB
inline constexpr int kEqValueMax = 63;     ///< -12 dB

/// `eq_type` (Winamp 5.51): 0 = Winamp 4Front curve (default), 1 = Constant-Q.
enum class EqMode {
    WinampCurve, ///< classic 4Front proportional-Q: wide at small gain, narrower at full boost/cut
    ConstantQ,   ///< Winamp 5.51+ Constant-Q: one Q for every band, independent of gain
};

/// Winamp band table (docs/spec.md §4.2), verified against VLC's
/// `equalizer_presets.h` `f_vlc_frequency_table_10b` (FE-2 cross-check).
inline constexpr std::array<double, kEqBands> kEqFrequencies{
    60.0, 170.0, 310.0, 600.0, 1000.0, 3000.0, 6000.0, 12000.0, 14000.0, 16000.0};

/// Q used by Constant-Q mode: a fixed ~one-octave bandwidth
/// (Q = 1/(2·sinh(ln2/2·1 octave))). Gain-independent by definition.
inline constexpr double kEqConstantQ = 1.4142135623730951;

/// Map a Winamp slider value to a band/preamp gain in dB.
/// 31 → 0 dB; 0 → +12 dB; 63 → −12 dB. Piecewise-linear because Winamp's
/// scale has 31 boost steps and 32 cut steps around the unity point.
inline double eqValueToDb(int value) noexcept {
    const int v = std::clamp(value, kEqValueMin, kEqValueMax);
    if (v <= kEqValueNeutral) {
        return static_cast<double>(kEqValueNeutral - v) * (12.0 / 31.0);
    }
    return -static_cast<double>(v - kEqValueNeutral) * (12.0 / 32.0);
}

/// Inverse of eqValueToDb, clamped to the slider field (for tests/round-trips).
inline int eqDbToValue(double db) noexcept {
    if (db >= 0.0) {
        const int steps = static_cast<int>(std::lround(db * (31.0 / 12.0)));
        return std::clamp(kEqValueNeutral - steps, kEqValueMin, kEqValueNeutral);
    }
    const int steps = static_cast<int>(std::lround(-db * (32.0 / 12.0)));
    return std::clamp(kEqValueNeutral + steps, kEqValueNeutral, kEqValueMax);
}

/// Filter Q for a band, given its gain and the selected curve mode.
///
/// * Constant-Q: `kEqConstantQ` for every band and every gain.
/// * Winamp curve: proportional Q — the bandwidth narrows from ~1 octave at
///   unity gain to ~1/3 octave at the ±12 dB extremes (the classic
///   analogue-graphic-EQ behaviour the 4Front EQ implements).
inline double bandQ(EqMode mode, double gainDb) noexcept {
    if (mode == EqMode::ConstantQ) {
        return kEqConstantQ;
    }
    constexpr double kLn2Half = 0.34657359027997264; // ln(2)/2
    const double mag = std::min(std::abs(gainDb), 12.0);
    const double bandwidthOctaves = 1.0 - (2.0 / 3.0) * (mag / 12.0);
    return 1.0 / (2.0 * std::sinh(kLn2Half * bandwidthOctaves));
}

/// Effective dB mapping of a preset (bands + preamp), for reporting/tests.
inline double eqPreampDb(int value) noexcept {
    return eqValueToDb(value);
}

} // namespace koalaamp::audio
