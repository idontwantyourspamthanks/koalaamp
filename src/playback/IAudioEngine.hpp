// SPDX-License-Identifier: MIT
// playback — audio engine + DSP pipeline abstraction (docs/spec.md §2.3, §3).
// Interface contract stub for FE-0; the libvlc implementation is FE-2.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace koalaamp::playback {

/// Interleaved float PCM format handed to the tap callback.
struct PcmFormat {
    int sampleRate{48000};
    int channels{2};
};

/// A reference to a playable medium. A stream is delivered separately via the
/// track-source abstraction; this covers the path/URL cases.
struct MediaRef {
    enum class Kind { Path, Url } kind{Kind::Path};
    std::string value;
};

/// The decode/playback seam. In DSP mode decoded PCM is delivered through
/// `setPcmCallback` before our DSP stage (docs/spec.md §3.1). No GUI calls may
/// ever happen on the audio thread.
class IAudioEngine {
public:
    using PcmCb =
        std::function<void(const float* interleaved, std::size_t frames, const PcmFormat&)>;

    virtual ~IAudioEngine() = default;

    virtual void setPcmCallback(PcmCb) = 0;
    virtual void play(const MediaRef&) = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seekMs(std::int64_t ms) = 0;
    virtual void setVolume(float volume) = 0; ///< 0.0 .. 1.0
    virtual bool isPlaying() const = 0;
};

} // namespace koalaamp::playback
