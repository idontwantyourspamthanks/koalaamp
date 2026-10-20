// SPDX-License-Identifier: MIT
// playback — audio engine + DSP pipeline abstraction (docs/spec.md §2.3, §3).
// Interface contract stub for FE-0; the libvlc implementation is FE-2.

#pragma once

#include "library/ITrackSource.hpp"
#include "playback/MediaStream.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace koalaamp::playback {

/// Interleaved float PCM format handed to the tap callback.
struct PcmFormat {
    int sampleRate{48000};
    int channels{2};
};

/// Server-side identity for `/Sessions/Playing*` reports (SV-2). A server media
/// ref carries one; local media leaves it empty.
struct ServerMediaInfo {
    std::string itemId;
    std::string mediaSourceId; ///< empty means "same as itemId"
    std::string playMethod;    ///< DirectPlay | Transcode
};

/// A reference to a playable medium: a local path, a locator (MRL), or a
/// callback-backed byte stream (SV-2 direct play — the engine reads it through
/// `libvlc_media_new_callbacks`, so no temp file and no URL credentials).
struct MediaRef {
    enum class Kind { Path, Url, Stream } kind{Kind::Path};
    std::string value;
    /// `Kind::Stream`: the re-openable byte source. `value` is then only a
    /// display label for errors and the UI, never a locator.
    std::shared_ptr<IMediaStreamSource> stream;
    /// ReplayGain hints carried from the source (server `NormalizationGain`);
    /// the prepare hook prefers them over reading tags off `value`.
    std::optional<library::Rg> rg;
    /// Present for media played from a server (SV-2 reporting).
    std::optional<ServerMediaInfo> server;
    /// Source metadata for UI/scrobbling when the caller has it. Local paths
    /// without this are resolved from tags by the scrobble wiring.
    std::optional<library::Track> track;
};

/// The decode/playback seam. In DSP mode decoded PCM is delivered through
/// `setPcmCallback` before our DSP stage (docs/spec.md §3.1). No GUI calls may
/// ever happen on the audio thread.
class IAudioEngine {
public:
    /// `interleaved` points at `frames * format.channels` float samples —
    /// `frames` counts FRAMES, not samples (so a 1024-frame stereo block is
    /// 2048 floats). Callers that construct their own buffer must size it
    /// `frames * channels`: getting this wrong is an out-of-bounds read
    /// (it caused an intermittent Windows SIGSEGV in a test that fed 1024
    /// floats while claiming 2 channels — ASAN-proven, see the SE-4 chain
    /// test). Engine-mediated callbacks are safe by construction: the engine
    /// builds the buffer from the format it decoded.
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
