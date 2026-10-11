// SPDX-License-Identifier: MIT
// audio — ReplayGain stage implementation. See ReplayGain.hpp.

#include "audio/ReplayGain.hpp"

#include <cmath>
#include <cstdlib>
#include <string>

namespace koalaamp::audio {

namespace {

/// strtod over a NUL-terminated copy; the trailing unit (" dB", " LUFS") and
/// surrounding whitespace are ignored. Returns empty when no number is present.
std::optional<double> parseLeadingDouble(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    const std::string copy(text);
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end == copy.c_str()) {
        return std::nullopt;
    }
    return value;
}

double linear(double db) noexcept {
    return std::pow(10.0, db / 20.0);
}

} // namespace

std::optional<double> parseGainDb(std::string_view text) {
    return parseLeadingDouble(text);
}

std::optional<double> parseLufs(std::string_view text) {
    return parseLeadingDouble(text);
}

RgResult computeReplayGain(const RgInfo& info, const RgConfig& config) noexcept {
    RgResult result;
    if (config.mode == RgMode::Off) {
        return result; // unity
    }

    std::optional<double> gain;
    std::optional<double> peak;

    const auto resolveGain = [](const std::optional<double>& db,
                                const std::optional<double>& lufs) -> std::optional<double> {
        if (db.has_value()) {
            return db;
        }
        if (lufs.has_value()) {
            return gainFromLufs(*lufs);
        }
        return std::nullopt;
    };

    if (config.mode == RgMode::Album) {
        gain = resolveGain(info.albumGainDb, info.albumLufs);
        peak = info.albumPeak;
        if (!gain.has_value()) { // album → track fallback (ReplayGain convention)
            gain = resolveGain(info.trackGainDb, info.trackLufs);
            peak = info.trackPeak;
        }
    } else {
        gain = resolveGain(info.trackGainDb, info.trackLufs);
        peak = info.trackPeak;
    }

    if (!gain.has_value()) {
        result.fallback = true;
        gain = config.fallbackDb;
    }

    double gainDb = *gain + config.preampDb;

    // Peak-aware clip prevention: never let a known peak cross full scale.
    if (config.preventClip && peak.has_value() && *peak > 0.0) {
        const double lin = linear(gainDb);
        if (*peak * lin > 1.0) {
            gainDb = -20.0 * std::log10(*peak);
            result.clipLimited = true;
        }
    }

    result.gainDb = gainDb;
    result.linearGain = linear(gainDb);
    return result;
}

} // namespace koalaamp::audio
