// SPDX-License-Identifier: MIT
// FE-14 spike helper — deterministic synthetic PCM so MilkDrop presets visibly
// react without real audio. Throwaway spike code, not a shipping component.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>

namespace fe14 {

// White-ish noise plus a 440 Hz fundamental and a 660 Hz (440*1.5) fifth, with
// a slow breathing envelope and a "kick" every 0.5 s. The kick drives projectM's
// internal beat detector and the bass/treble bands, so presets that key off
// bass_att / treb_att / beat move instead of sitting flat.
class SyntheticPcm {
public:
    explicit SyntheticPcm(double sampleRate = 48000.0, std::uint32_t seed = 0xC0FFEEu)
        : sampleRate_(sampleRate), rng_(seed) {}

    // Fill `frames` interleaved stereo frames into `out` (2*frames floats).
    void fill(float* out, std::size_t frames) {
        for (std::size_t i = 0; i < frames; ++i) {
            const double t = time_;
            const double breathe = 0.6 + 0.4 * std::sin(2.0 * kPi * 2.0 * t);
            const double beat = std::exp(-6.0 * std::fmod(t, 0.5));
            const double env = 0.45 * breathe + 0.55 * beat;
            const auto sample = static_cast<float>(env * (0.34 * std::sin(2.0 * kPi * 440.0 * t) +
                                                          0.24 * std::sin(2.0 * kPi * 660.0 * t) +
                                                          0.10 * noise_(rng_)));
            out[2 * i] = sample;
            out[2 * i + 1] = sample;
            time_ += 1.0 / sampleRate_;
        }
    }

    double sampleRate() const { return sampleRate_; }

private:
    static constexpr double kPi = 3.14159265358979323846;
    double sampleRate_;
    double time_ = 0.0;
    std::mt19937 rng_;
    std::normal_distribution<float> noise_{0.0f, 1.0f};
};

} // namespace fe14
