// SPDX-License-Identifier: MIT
#include "ui/VisData.hpp"

#include <algorithm>
#include <cmath>

namespace koalaamp::ui {

namespace {

constexpr double kPi = 3.14159265358979323846;
/// Bands clamp to `a` (Webamp: `a = 15` for the 1x main window — the 16 px vis
/// area minus the pushDown of 2).
constexpr int kBandMax = 15;
/// Webamp's oscilloscope window: the first 576 of the 1024 analysed samples,
/// sampled every `576 / 75` frames (integer division, = 7).
constexpr int kOscSamples = 576;
constexpr int kOscStride = kOscSamples / kVisBars;
/// Rows of the main-window vis area (Webamp's `renderHeight`).
constexpr int kVisRows = 16;

/// Webamp feeds `getByteTimeDomainData` (128 + 127.5*value) into
/// `(byte - 128) / 24`.
std::uint8_t byteOf(float sample) {
    return static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(std::lround(sample * 127.5F)) + 128, 0, 255));
}

}  // namespace

WinampFft::WinampFft() {
    // Bit-reversal permutation, built the way Webamp builds it.
    for (int i = 0; i < kSize; ++i) {
        m_bitRev[static_cast<std::size_t>(i)] = i;
    }
    int a = 0;
    for (int n = 0; n < kSize; ++n) {
        if (a > n) {
            std::swap(m_bitRev[static_cast<std::size_t>(n)], m_bitRev[static_cast<std::size_t>(a)]);
        }
        int r = kSize >> 1;
        while (r >= 1 && a >= r) {
            a -= r;
            r >>= 1;
        }
        a += r;
    }

    // Twiddle factors for stages 2, 4, ... 1024.
    int index = 0;
    for (int size = 2; size <= kSize; size <<= 1) {
        const double angle = -2.0 * kPi / static_cast<double>(size);
        m_cosSin[static_cast<std::size_t>(index)][0] = static_cast<float>(std::cos(angle));
        m_cosSin[static_cast<std::size_t>(index)][1] = static_cast<float>(std::sin(angle));
        ++index;
    }

    // Window: (0.5 + 0.5*sin(2*pi*i/N - pi/2))^1.
    for (int i = 0; i < kSize; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSize) * 2.0 * kPi - kPi / 2.0;
        m_envelope[static_cast<std::size_t>(i)] =
            static_cast<float>(std::pow(0.5 + 0.5 * std::sin(t), 1.0));
    }

    // Equalise table: `a` decays geometrically and the step is recomputed from
    // the *current* `a` on every iteration (Webamp's shadowed `const t`).
    float decay = 0.04F;
    for (int i = 0; i < kBins; ++i) {
        const float step = (9.0F - decay) / static_cast<float>(kBins);
        m_equalize[static_cast<std::size_t>(i)] =
            std::log10(1.0F + decay + static_cast<float>(i + 1) * step);
        decay /= 1.0025F;
    }
}

void WinampFft::transform(const float* samples, float* out) const {
    for (int i = 0; i < kSize; ++i) {
        const int source = m_bitRev[static_cast<std::size_t>(i)];
        m_tmp1[static_cast<std::size_t>(i)] =
            samples[source] * m_envelope[static_cast<std::size_t>(source)];
    }
    std::fill(m_tmp2.begin(), m_tmp2.end(), 0.0F);

    float* re = m_tmp1.data();
    float* im = m_tmp2.data();
    int groupSize = 2;
    int stage = 0;
    while (groupSize <= kSize) {
        const float cosA = m_cosSin[static_cast<std::size_t>(stage)][0];
        const float sinA = m_cosSin[static_cast<std::size_t>(stage)][1];
        float wr = 1.0F;
        float wi = 0.0F;
        const int half = groupSize >> 1;
        for (int k = 0; k < half; ++k) {
            for (int j = k; j < kSize; j += groupSize) {
                const int other = j + half;
                const float tr = wr * re[other] - wi * im[other];
                const float ti = wr * im[other] + wi * re[other];
                re[other] = re[j] - tr;
                im[other] = im[j] - ti;
                re[j] += tr;
                im[j] += ti;
            }
            const float prev = wr;
            wr = wr * cosA - wi * sinA;
            wi = wi * cosA + prev * sinA;
        }
        groupSize <<= 1;
        ++stage;
    }

    for (int i = 0; i < kBins; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        out[idx] = std::sqrt(re[idx] * re[idx] + im[idx] * im[idx]) * m_equalize[idx];
    }
}

VisAnalyzer::VisAnalyzer(VisAnalyzerConfig config) : m_config(config) { reset(); }

void VisAnalyzer::reset() {
    m_timeDomain.fill(0.0F);
    m_spectral.fill(0.0F);
    m_sample.fill(0.0F);
    m_data.fill(0);
    m_peaks.fill(0);
    m_falloff.fill(0.0F);
    m_data2.fill(0.0F);
}

void VisAnalyzer::analyze(const float* samples, std::size_t count, VisFrame& frame) {
    // The analysed window is the most recent kSize samples, left-padded with
    // zeros when the stream is younger than that (Webamp's analyser buffer).
    const std::size_t available = std::min(count, static_cast<std::size_t>(WinampFft::kSize));
    const std::size_t pad = m_bytes.size() - available;
    for (std::size_t i = 0; i < m_bytes.size(); ++i) {
        const std::size_t source = (i >= pad) ? (i - pad) : count;
        m_bytes[i] = source < count ? byteOf(samples[source]) : 128U;
        m_timeDomain[i] = (static_cast<float>(m_bytes[i]) - 128.0F) / 24.0F;
    }
    m_fft.transform(m_timeDomain.data(), m_spectral.data());

    // 75 logarithmically spaced bands with linear interpolation between bins.
    const double log512 = std::log10(512.0);
    for (int i = 0; i < kVisBars; ++i) {
        const double fraction = static_cast<double>(i) / static_cast<double>(kVisBars - 1);
        const double position = fraction * 511.0 * 0.09 + 0.91 * std::pow(10.0, log512 * fraction);
        int low = static_cast<int>(std::floor(position));
        int high = static_cast<int>(std::ceil(position));
        low = std::min(low, WinampFft::kBins - 1);
        high = std::min(high, WinampFft::kBins - 1);
        const auto lo = static_cast<std::size_t>(low);
        const auto hi = static_cast<std::size_t>(high);
        if (low == high) {
            m_sample[static_cast<std::size_t>(i)] = m_spectral[lo];
        } else {
            const double frac = position - low;
            m_sample[static_cast<std::size_t>(i)] =
                static_cast<float>((1.0 - frac) * m_spectral[lo] + frac * m_spectral[hi]);
        }
    }

    frame.barsValid = true;
    for (int i = 0; i < kVisBars; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        const int chunk = m_config.wideBandwidth ? (i & ~3) : i;
        float band = m_sample[idx];
        if (m_config.wideBandwidth) {
            const auto base = static_cast<std::size_t>(chunk);
            band = (m_sample[base] + m_sample[base + 1] + m_sample[base + 2] + m_sample[base + 3]) / 4.0F;
        }
        auto data = static_cast<std::int16_t>(band);
        if (data >= kBandMax) {
            data = static_cast<std::int16_t>(kBandMax);
        }
        m_data[idx] = data;
        if (m_peaks[idx] >= 256 * kBandMax) {
            m_peaks[idx] = static_cast<std::int16_t>(256 * kBandMax);
        }
        m_falloff[idx] -= static_cast<float>(m_config.falloff) / 16.0F;
        if (m_falloff[idx] <= static_cast<float>(m_data[idx])) {
            m_falloff[idx] = static_cast<float>(m_data[idx]);
        }
        const auto peakScaled = static_cast<std::int16_t>(std::lround(256.0F * m_falloff[idx]));
        if (m_peaks[idx] <= peakScaled) {
            m_peaks[idx] = peakScaled;
            m_data2[idx] = 3.0F;
        }
        float barPeak = static_cast<float>(m_peaks[idx]) / 256.0F;
        m_peaks[idx] = static_cast<std::int16_t>(m_peaks[idx] - std::lround(m_data2[idx]));
        m_data2[idx] *= m_config.peakFalloff;
        if (m_peaks[idx] <= 0) {
            m_peaks[idx] = 0;
        }
        const bool barHidden = std::lround(barPeak) < 1;
        // Winamp "wide" bandwidth never paints the fourth column of each group.
        const bool columnSkipped = m_config.wideBandwidth && (i & 3) == 3;
        const int barHeight = std::max(0, static_cast<int>(std::lround(m_falloff[idx])));
        frame.bars[idx] = (barHidden || columnSkipped)
                              ? 0
                              : static_cast<std::uint8_t>(std::min(barHeight, 255));
        const int peakHeight =
            std::clamp(static_cast<int>(std::lround(barPeak)), 0, kBandMax);
        frame.peaks[idx] = (m_config.peaks && !barHidden && !columnSkipped)
                               ? static_cast<std::uint8_t>(peakHeight)
                               : kPeakOff;
    }

    // Oscilloscope: 75 points sampled every 7 frames of the first 576 samples
    // of the analysed window (Webamp `VC::paintOscilloscope`).
    frame.oscValid = true;
    for (int i = 0; i < kVisBars; ++i) {
        const auto source = static_cast<std::size_t>(i * kOscStride);
        const int value = source < kOscSamples ? static_cast<int>(m_bytes[source]) : 128;
        int y = static_cast<int>(std::lround(static_cast<double>(value) / 16.0 * 2.0)) - 9;
        y = std::clamp(y, 0, kVisRows - 1);
        frame.osc[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(y);
    }
}

PcmRing::PcmRing(std::size_t capacityFrames) {
    std::size_t capacity = 1024;
    while (capacity < capacityFrames) {
        capacity <<= 1;
    }
    m_data.assign(capacity, 0.0F);
    m_mask = capacity - 1;
}

void PcmRing::push(const float* interleaved, std::size_t frames, int channels) noexcept {
    if (interleaved == nullptr || frames == 0 || channels <= 0) {
        return;
    }
    std::uint64_t write = m_write.load(std::memory_order_relaxed);
    const auto channelCount = static_cast<std::size_t>(channels);
    for (std::size_t f = 0; f < frames; ++f) {
        float sum = 0.0F;
        for (std::size_t c = 0; c < channelCount; ++c) {
            sum += interleaved[f * channelCount + c];
        }
        m_data[static_cast<std::size_t>(write) & m_mask] = sum / static_cast<float>(channelCount);
        ++write;
    }
    m_write.store(write, std::memory_order_release);
}

std::size_t PcmRing::readRecent(float* out, std::size_t outSize) const noexcept {
    if (out == nullptr || outSize == 0) {
        return 0;
    }
    const std::uint64_t end = m_write.load(std::memory_order_acquire);
    const std::size_t available =
        static_cast<std::size_t>(std::min<std::uint64_t>(end, m_data.size() - 1));
    const std::size_t requested = std::min(outSize, available);
    for (std::size_t i = 0; i < requested; ++i) {
        // Oldest-first within the requested window.
        const std::uint64_t index = end - requested + i;
        out[i] = m_data[static_cast<std::size_t>(index) & m_mask];
    }
    return requested;
}

void PcmRing::clear() noexcept {
    std::fill(m_data.begin(), m_data.end(), 0.0F);
    m_write.store(0, std::memory_order_release);
}

VisFrame syntheticVisFrame(int step) {
    VisFrame frame;
    frame.barsValid = true;
    frame.oscValid = true;
    const double phase = static_cast<double>(step) * 0.2;
    for (int i = 0; i < kVisBars; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        const double wave = std::sin(phase + static_cast<double>(i) * 0.35);
        const int bar = std::clamp(static_cast<int>(std::lround(7.5 + 7.5 * wave)), 0, kBandMax);
        frame.bars[idx] = static_cast<std::uint8_t>(bar);
        frame.peaks[idx] =
            bar <= 1 ? kPeakOff : static_cast<std::uint8_t>(std::min(kBandMax, bar + 3));
        const double oscWave = std::sin(phase * 1.5 + static_cast<double>(i) * 0.5);
        frame.osc[idx] =
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(
                                                   7.5 + 7.5 * oscWave)), 0, kVisRows - 1));
    }
    return frame;
}

}  // namespace koalaamp::ui
