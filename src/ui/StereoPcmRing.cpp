// SPDX-License-Identifier: MIT
#include "ui/StereoPcmRing.hpp"

#include <algorithm>

namespace koalaamp::ui {

StereoPcmRing::StereoPcmRing(std::size_t capacityFrames) {
    std::size_t frames = 1024;
    while (frames < capacityFrames) {
        frames <<= 1;
    }
    m_mask = frames - 1;
    m_data.assign(frames * 2, 0.0F);
}

void StereoPcmRing::push(const float* interleaved, std::size_t frames, int channels) noexcept {
    // The audio thread: no allocation, no lock, no GUI. Anything odd is dropped
    // rather than reported — a visualizer must never disturb playback.
    if (interleaved == nullptr || frames == 0 || channels <= 0) {
        return;
    }
    const auto channelCount = static_cast<std::size_t>(channels);
    std::uint64_t write = m_write.load(std::memory_order_relaxed);
    for (std::size_t f = 0; f < frames; ++f) {
        const float* frame = interleaved + f * channelCount;
        const float left = frame[0];
        const float right = channelCount >= 2 ? frame[1] : frame[0];
        const std::size_t slot = (static_cast<std::size_t>(write) & m_mask) * 2;
        m_data[slot] = left;
        m_data[slot + 1] = right;
        ++write;
    }
    m_write.store(write, std::memory_order_release);
}

std::size_t StereoPcmRing::readRecent(float* out, std::size_t maxFrames) const noexcept {
    if (out == nullptr || maxFrames == 0) {
        return 0;
    }
    const std::uint64_t end = m_write.load(std::memory_order_acquire);
    const auto capacityFrames = m_mask + 1;
    // One frame is unusable so the reader can never confuse full with empty, and
    // a producer that outruns the reader simply loses the oldest audio.
    const std::size_t available =
        static_cast<std::size_t>(std::min<std::uint64_t>(end, capacityFrames - 1));
    const std::size_t requested = std::min(maxFrames, available);
    for (std::size_t i = 0; i < requested; ++i) {
        // Oldest-first within the requested window.
        const std::uint64_t frame = end - requested + i;
        const std::size_t slot = (static_cast<std::size_t>(frame) & m_mask) * 2;
        out[i * 2] = m_data[slot];
        out[i * 2 + 1] = m_data[slot + 1];
    }
    return requested;
}

void StereoPcmRing::clear() noexcept { m_write.store(0, std::memory_order_release); }

}  // namespace koalaamp::ui
