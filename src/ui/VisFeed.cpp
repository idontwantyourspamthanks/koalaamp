// SPDX-License-Identifier: MIT
#include "ui/VisFeed.hpp"

namespace koalaamp::ui {

VisFeed::VisFeed(std::size_t capacityFrames) : m_ring(capacityFrames) {}

playback::IAudioEngine::PcmCb VisFeed::callback() {
    return [this](const float* interleaved, std::size_t frames, const playback::PcmFormat& format) {
        // Audio thread: no allocation, no lock, no GUI.
        m_channels.store(format.channels, std::memory_order_relaxed);
        m_sampleRate.store(format.sampleRate, std::memory_order_relaxed);
        m_ring.push(interleaved, frames, format.channels);
    };
}

void VisFeed::sample(VisFrame& frame) {
    const std::size_t count = m_ring.readRecent(m_scratch.data(), m_scratch.size());
    m_analyzer.analyze(m_scratch.data(), count, frame);
}

void VisFeed::reset() {
    m_ring.clear();
    m_analyzer.reset();
    m_channels.store(0, std::memory_order_relaxed);
    m_sampleRate.store(0, std::memory_order_relaxed);
}

}  // namespace koalaamp::ui
