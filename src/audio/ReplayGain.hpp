// SPDX-License-Identifier: MIT
// audio — ReplayGain stage (docs/spec.md §4.3).
//
// Modes off/album/track, a user preamp, peak-aware clipping prevention, and a
// −6 dB fallback for untagged files. R128/LUFS loudness values are normalised
// to a gain with `gain = −18 − LUFS`; the Opus/R128 Q7.8 tag form is also
// accepted. Tag values arrive as strings from the tag reader (AU-2/FC tickets
// own the reader); this header owns the arithmetic and the string parsing.

#pragma once

#include <optional>
#include <string_view>

namespace koalaamp::audio {

enum class RgMode { Off, Track, Album };

/// ReplayGain/R128 information for one track. Gains are dB, peaks are linear
/// (0..N), LUFS is the R128 integrated loudness.
struct RgInfo {
    std::optional<double> trackGainDb;
    std::optional<double> trackPeak;
    std::optional<double> albumGainDb;
    std::optional<double> albumPeak;
    std::optional<double> trackLufs;
    std::optional<double> albumLufs;
};

struct RgConfig {
    RgMode mode{RgMode::Off};
    double preampDb{0.0};
    bool preventClip{true};
    double fallbackDb{-6.0}; ///< untagged files (docs/spec.md §4.3)
};

struct RgResult {
    double gainDb{0.0};
    double linearGain{1.0};
    bool fallback{false};    ///< no usable tag → fallbackDb applied
    bool clipLimited{false}; ///< peak-aware reduction applied
};

/// gain (dB) implied by an R128 integrated loudness in LUFS (docs/spec.md §4.3).
inline double gainFromLufs(double lufs) noexcept {
    return -18.0 - lufs;
}

/// R128_*_GAIN tag (Q7.8, 1/256 dB) → dB.
inline double gainFromR128Q78(int value) noexcept {
    return static_cast<double>(value) / 256.0;
}

/// Parse a ReplayGain gain string such as "-9.50 dB" / "+3.2dB" / "-9.5".
std::optional<double> parseGainDb(std::string_view text);

/// Parse an R128 loudness string such as "-23.0 LUFS" / "-23.0".
std::optional<double> parseLufs(std::string_view text);

/// Resolve the applied gain for a track. Never throws; allocation-free.
RgResult computeReplayGain(const RgInfo& info, const RgConfig& config) noexcept;

} // namespace koalaamp::audio
