// SPDX-License-Identifier: MIT
//
// PcmFanout — one PCM stream, several consumers (docs/spec.md §2.1, §6.2).
//
// The DSP output already has a reader (the main window's VisFeed spectrum) and
// the visualizer needs the same samples as raw stereo PCM. A second reader must
// not be added by touching the engine's tap: VisFeed's contract is "DSP plugs
// setPcmCallback, never re-plumbs", and installing a callback again would
// *replace* the DSP chain — silently turning off the EQ / ReplayGain stage while
// both consumers still looked alive. So the chain fans out after the DSP
// instead: `dsp->makeCallback(fanout.callback())`.
//
// Every consumer inherits VisFeed's audio-thread rules: allocate nothing, take no
// lock, touch no GUI. Subscribe before audio starts; the push path only walks a
// vector whose storage is already sized.
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "playback/IAudioEngine.hpp"

namespace koalaamp::ui {

/// Forwards each pushed buffer to every subscriber, in subscription order.
class PcmFanout {
public:
    /// Subscribe a consumer. Call during setup, not while audio is flowing.
    void add(playback::IAudioEngine::PcmCb consumer) {
        if (consumer) {
            m_consumers.push_back(std::move(consumer));
        }
    }

    std::size_t consumerCount() const { return m_consumers.size(); }

    /// The composite callback to hand to the DSP's `makeCallback`.
    playback::IAudioEngine::PcmCb callback() {
        return [this](const float* interleaved, std::size_t frames,
                      const playback::PcmFormat& format) {
            for (const auto& consumer : m_consumers) {
                consumer(interleaved, frames, format);
            }
        };
    }

private:
    std::vector<playback::IAudioEngine::PcmCb> m_consumers;
};

}  // namespace koalaamp::ui
