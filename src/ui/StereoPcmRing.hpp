// SPDX-License-Identifier: MIT
//
// StereoPcmRing — the visualizer's end of the PCM tap: a lock-free ring of
// *stereo* interleaved frames, written by the audio thread and consumed by the GL
// thread that feeds projectM.
//
// Why not reuse PcmRing: that one down-mixes to mono for the spectrum analyser,
// and MilkDrop is a stereo effect — projectM is fed interleaved L/R and uses the
// difference between the channels. This ring therefore keeps two floats per frame
// always: mono input is duplicated into both channels, and anything wider than
// stereo keeps its first two. A fixed stride also means there is no channel count
// for the reader to race the writer over.
//
// The reader *consumes*: each readNew() returns the frames that arrived since the
// previous one, so the visualizer hands projectM every sample exactly once. If
// the reader falls behind, the oldest frames are dropped rather than blocking the
// audio thread (docs/tickets/VZ-1: audio correctness beats visual completeness).
//
// Same discipline as PcmRing and VisFeed's callback: the write path allocates
// nothing, takes no lock and never touches the GUI; only the write index is
// shared (docs/spec.md §2.1). The read cursor belongs to the GL thread alone.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace koalaamp::ui {

class StereoPcmRing {
public:
    /// `capacityFrames` is rounded up to a power of two (minimum 1024).
    explicit StereoPcmRing(std::size_t capacityFrames = 1 << 14);

    /// Appends `frames` interleaved frames as stereo (audio thread).
    void push(const float* interleaved, std::size_t frames, int channels) noexcept;

    /// Copies up to `maxFrames` frames that have arrived since the previous read —
    /// 2 floats each, oldest first — and returns how many were written (GL thread).
    std::size_t readNew(float* out, std::size_t maxFrames) noexcept;

    /// Drops the buffered audio and rewinds the reader (called when playback
    /// stops, never mid-callback).
    void clear() noexcept;
    std::size_t capacity() const { return m_mask + 1; }

private:
    std::vector<float> m_data;              ///< 2 floats per frame
    std::size_t m_mask = 0;                 ///< frame index mask (capacity - 1)
    std::atomic<std::uint64_t> m_write{0};  ///< frames written since the last clear
    std::uint64_t m_read = 0;               ///< GL-thread cursor into the same count
};

}  // namespace koalaamp::ui
