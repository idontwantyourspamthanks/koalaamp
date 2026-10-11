// SPDX-License-Identifier: MIT
//
// VisFeed — the window's end of the PCM tap: a lock-free ring the audio thread
// writes and the analyser reads on the GUI thread.
//
// The tap callback is installed through `IAudioEngine::setPcmCallback` — the
// sanctioned DSP-insertion seam (docs/playback: "DSP plugs setPcmCallback,
// never re-plumbs"); nothing here touches the engine's plumbing. The callback
// allocates nothing, takes no lock and never touches the GUI (docs/spec.md
// §2.1); only the ring's head pointer is shared.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

#include "playback/IAudioEngine.hpp"
#include "ui/VisData.hpp"

namespace koalaamp::ui {

/// Long enough to hold the analyser window several times over, so a stalled GUI
/// still gets a full 1024-sample window.
inline constexpr std::size_t kVisRingFrames = 1 << 14;

class VisFeed {
public:
    explicit VisFeed(std::size_t capacityFrames = kVisRingFrames);

    /// The callback to hand to `IAudioEngine::setPcmCallback`. Safe to call
    /// while audio is flowing: it only writes to the ring and two atomics.
    playback::IAudioEngine::PcmCb callback();

    /// Analyses the newest samples into `frame` (GUI thread). With no audio
    /// yet the frame is all zeros — which is exactly what Winamp draws for a
    /// silent stream (background + dither dots, no bars).
    void sample(VisFrame& frame);

    /// Channel count / sample rate of the stream being decoded (0 until audio
    /// flows). These drive the monoster indicator and the `khz` field.
    int channels() const { return m_channels.load(); }
    int sampleRate() const { return m_sampleRate.load(); }

    /// Called when playback stops so a new track re-reads its format.
    void reset();

private:
    PcmRing m_ring;
    VisAnalyzer m_analyzer;
    std::array<float, WinampFft::kSize> m_scratch{};
    std::atomic<int> m_channels{0};
    std::atomic<int> m_sampleRate{0};
};

}  // namespace koalaamp::ui
