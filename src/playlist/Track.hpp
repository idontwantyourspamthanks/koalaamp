// SPDX-License-Identifier: MIT
//
// Track — the serialisable playlist row (docs/tickets/PL-1-playlist-formats.md).
//
// Deliberately a plain value type with no Qt and no dependency on the UI: the
// playlist module is the *serialisation* layer (files <-> rows), and `src/ui`
// converts `PleditEntry` <-> `Track` at the window verbs. Keeping the model
// here means every format writer/reader shares one shape and the round-trip
// tests compare like with like.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace koalaamp::playlist {

/// One playlist row. `lengthMs` is milliseconds (0 = unknown); on the wire
/// M3U/PLS carry whole seconds and B4S/XSPF carry milliseconds, so the
/// lossless common denominator is ms.
struct Track {
    std::string title;
    std::string filename;  ///< path or URL, as stored in the playlist
    int lengthMs = 0;
    bool operator==(const Track&) const = default;
};

/// The formats this module speaks. `M3u8` is M3U *with* the UTF-8 BOM.
enum class Format { Unknown, M3u, M3u8, Pls, B4s, Xspf, Asx, Wpl };

struct ReadOptions {
    /// The playlist file's own folder: relative media references resolve
    /// against it (Winamp M3ULoader behaviour).
    std::filesystem::path baseDir{};
    /// Caller hint that the bytes are UTF-8 (e.g. the `.m3u8` extension). The
    /// reader still honours a BOM regardless.
    bool utf8 = false;
};

/// A parsed playlist: the detected format plus its rows.
struct Playlist {
    Format format = Format::Unknown;
    std::vector<Track> tracks;
};

}  // namespace koalaamp::playlist
