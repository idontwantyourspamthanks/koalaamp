// SPDX-License-Identifier: MIT
// audio — minimal clean-room ReplayGain/R128 tag reader (AU-2).
// LI-1 seam: this is the thin reader the playback wiring uses to populate
// DspPipeline::setReplayGain(); LI-1's TagLib pipeline replaces it behind the
// same API. Formats implemented from their public documentation only: Vorbis
// comments (FLAC/Ogg-Vorbis/Ogg-Opus) and ID3v2.2/2.3/2.4 TXXX frames.
//
// Clean-room: written from the public FLAC, Ogg, Vorbis-comment, OpusTags and
// ID3v2 specifications; no WCL/GPL source consulted. Only the `TXXX` ID3 frame
// is inspected — that is the frame ReplayGain/R128 taggers use; text frames
// such as `TIT2` are deliberately ignored. Limitations: ID3v2.3/2.4 extended
// headers are not implemented (a file carrying one yields no fields), and the
// reader never validates the Ogg page CRC.

#pragma once

#include "audio/ReplayGain.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace koalaamp::audio {

/// Which container tag form the values came from (diagnostics/tests).
enum class RgTagContainer { None, VorbisComment, Id3v2 };

struct RgTagRead {
    RgInfo info;
    RgTagContainer container{RgTagContainer::None};
    /// True when at least one ReplayGain/R128 field was present (note: a tag
    /// can legitimately be present and empty, so this is "found", not "usable").
    bool any() const noexcept { return container != RgTagContainer::None; }
};

/// Parse ReplayGain/R128 tags from an in-memory container header (tests +
/// the file overload share this code path). Never throws.
RgTagRead parseReplayGainTags(const std::uint8_t* data, std::size_t size);

/// Read up to 4 MiB from the start of `path` and parse its tags. Missing /
/// unreadable file or malformed container -> an empty RgTagRead (None).
/// Never throws.
RgTagRead readReplayGainTags(const std::string& path);

/// The cache the wiring layer uses so a track's tags are read once. Single-
/// threaded use (the playback hook runs on one dispatch thread).
class RgTagSource {
public:
    /// Cached ReplayGain info for a local file; empty RgInfo when untagged.
    /// Never throws.
    RgInfo forPath(const std::string& path);
    void clear();
    std::size_t size() const noexcept;

private:
    /// Bound the cache so a long session cannot grow without limit. When the
    /// cap is hit the map is cleared wholesale (no LRU bookkeeping needed for
    /// a playback hook that re-reads at most one path per track).
    static constexpr std::size_t kMaxEntries = 4096;
    std::unordered_map<std::string, RgInfo> m_cache;
};

} // namespace koalaamp::audio
