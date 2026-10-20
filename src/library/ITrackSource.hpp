// SPDX-License-Identifier: MIT
// library — track model + source abstraction (docs/spec.md §2.3, §7).
// Interface contract stub for FE-0; implemented by LI-* / SV-* tickets.

#pragma once

#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace koalaamp::library {

enum class SourceId { Local, Jellyfin, Plex, Playlist };

/// ReplayGain tags, normalized across tag formats (docs/spec.md §4.3).
struct Rg {
    double trackGain{0.0};
    double trackPeak{0.0};
    double albumGain{0.0};
    double albumPeak{0.0};
};

/// One playable item. Mirrors the spec's Track contract; the SQLite schema in
/// §7.2 maps onto it.
struct Track {
    SourceId src{SourceId::Local};
    std::string id;
    std::string path;
    std::string title;
    std::string artist;
    std::string albumArtist;
    std::string album;
    std::string genre;
    std::string comment;
    int bpm{0};
    int year{0};
    int trackNo{0};
    int discNo{0};
    int rating{0};
    int playCount{0};
    std::int64_t lastPlayed{0}; ///< Unix seconds; 0 = never
    std::int64_t dateAdded{0};  ///< Unix seconds
    std::int64_t durationMs{0};
    /// Container/codec hint from a server item ("flac", "mp3", "aac", …), used
    /// by SV-2's direct-vs-transcode decision. Empty when unknown.
    std::string codec;
    /// Source bitrate in bits/s when the server reports one; 0 = unknown.
    std::int64_t bitrate{0};
    /// MusicBrainz identities, when the source/tags supply them (SC-1).
    std::string recordingMbid;
    std::vector<std::string> artistMbids;
    std::string releaseMbid;
    std::optional<Rg> rg;
};

/// A library/search query. Full grammar (Winamp smart views) lands in LI-3.
struct Query {
    std::string text;
    int limit{0}; ///< 0 = unbounded
};

enum class StreamIntent { Playback, Download };

/// Cooperative cancellation token passed into long-running operations.
class Cancellation {
public:
    virtual ~Cancellation() = default;
    virtual bool requested() const = 0;
};

/// Anything that can enumerate and stream tracks: local library, a server
/// gateway, or a playlist. Implementations must not throw across module
/// boundaries; failures surface as empty results / null streams.
class ITrackSource {
public:
    virtual ~ITrackSource() = default;

    virtual std::vector<Track> search(const Query&, Cancellation&) = 0;
    virtual std::unique_ptr<std::istream> openStream(const Track&, StreamIntent) = 0;
    virtual void setRating(const Track&, int stars) = 0;
};

} // namespace koalaamp::library
