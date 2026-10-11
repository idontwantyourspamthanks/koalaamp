// SPDX-License-Identifier: MIT
// interop/vlc — allocation-free metrics recorder for audio-thread sampling.
//
// Single writer (an audio callback), read after the stream stops. Capacity is
// fixed up front; overflow is counted rather than allocated for.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace koalaamp::interop::vlc {

class SampleRecorder {
public:
    void reset(std::size_t capacity) {
        m_samples.assign(capacity, 0);
        m_count.store(0, std::memory_order_relaxed);
    }

    void add(std::uint32_t value) noexcept {
        const std::size_t i = m_count.fetch_add(1, std::memory_order_relaxed);
        if (i < m_samples.size()) {
            m_samples[i] = value;
        }
    }

    std::size_t size() const noexcept {
        return std::min(m_count.load(std::memory_order_relaxed), m_samples.size());
    }

    bool overflowed() const noexcept {
        return m_count.load(std::memory_order_relaxed) > m_samples.size();
    }

    std::uint32_t max() const noexcept {
        std::uint32_t m = 0;
        const std::size_t n = size();
        for (std::size_t i = 0; i < n; ++i) {
            m = std::max(m, m_samples[i]);
        }
        return m;
    }

    /// Nearest-rank percentile over the recorded samples (0..1). 0 if empty.
    double percentile(double p) const {
        const std::size_t n = size();
        if (n == 0) {
            return 0.0;
        }
        std::vector<std::uint32_t> s(m_samples.begin(), m_samples.begin() + n);
        std::sort(s.begin(), s.end());
        const std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(n - 1) + 0.5);
        return static_cast<double>(s[idx]);
    }

private:
    std::vector<std::uint32_t> m_samples;
    std::atomic<std::size_t> m_count{0};
};

} // namespace koalaamp::interop::vlc
