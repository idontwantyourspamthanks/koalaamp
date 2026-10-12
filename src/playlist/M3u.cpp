// SPDX-License-Identifier: MIT
//
// M3U / M3U8 — Winamp's primary playlist format (docs/research/formats.md §1).
//
// Writer (byte-exact, from the source-verified `M3UWriter.cpp`/`M3U8Writer.cpp`
// emission): header `#EXTM3U\n`; per track with metadata
// `#EXTINF:<length_seconds>,<title>\n<filename>\n`; without metadata just
// `<filename>\n`. M3U8 is the same bytes preceded by the UTF-8 BOM
// (`EF BB BF`) — *before* `#EXTM3U`.
//
// Reader (lenient, the `M3ULoader.cpp` behaviour): a BOM or the `.m3u8`
// extension selects UTF-8; otherwise invalid UTF-8 is widened as Latin-1.
// `#EXTM3U`, `#EXTINF:`, `#UTF8` and `#EXT-X-NS-CLOUD:` are recognised; every
// other `#` line is ignored. Blank lines, CRLF/LF and surrounding whitespace
// are tolerated. An `ASF ` prefix is stripped from a media line; relative
// references resolve against the playlist's own folder.
//
// `#EXTAUTOSTART` is deliberately NOT handled: it is not a Winamp extension
// (formats.md §1).
#include "playlist/Playlist.hpp"

#include "playlist/Util.hpp"

#include <string>
#include <string_view>

namespace koalaamp::playlist {
namespace {

bool startsWithIgnoreCase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) {
        return false;
    }
    return equalsIgnoreCase(s.substr(0, prefix.size()), prefix);
}

}  // namespace

std::string writeM3u(const std::vector<Track>& tracks, bool utf8) {
    std::string out;
    if (utf8) {
        out += "\xEF\xBB\xBF";
    }
    out += "#EXTM3U\n";
    for (const Track& track : tracks) {
        if (!track.title.empty() || track.lengthMs != 0) {
            long seconds = track.lengthMs / 1000;
            if (seconds < 0) {
                seconds = 0;
            }
            out += "#EXTINF:";
            out += std::to_string(seconds);
            out += ',';
            out += track.title;
            out += '\n';
        }
        out += track.filename;
        out += '\n';
    }
    return out;
}

std::vector<Track> readM3u(std::string_view bytes, const ReadOptions& opts) {
    std::string_view body = stripBom(bytes);
    // Decode once; toValidUtf8 is the identity for valid input and a Latin-1
    // widening otherwise, so a non-UTF-8 (Windows ANSI) playlist still reads.
    const std::string text = toValidUtf8(body);
    (void)opts.utf8;  // the BOM/validity check already subsumes the hint

    std::vector<Track> tracks;
    bool havePending = false;
    bool pendingTitlePresent = false;
    std::string pendingTitle;
    int pendingLengthMs = 0;

    for (std::string_view raw : splitLines(text)) {
        const std::string_view line = trim(raw);
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            if (startsWithIgnoreCase(line, "#EXTINF:")) {
                std::string_view rest = line.substr(8);
                const std::size_t comma = rest.find(',');
                const std::string_view secondsPart = comma == std::string_view::npos
                                                         ? rest
                                                         : rest.substr(0, comma);
                long seconds = 0;
                if (parseLong(trim(secondsPart), seconds) && seconds > 0) {
                    pendingLengthMs = static_cast<int>(seconds * 1000);
                } else {
                    pendingLengthMs = 0;
                }
                pendingTitlePresent = comma != std::string_view::npos;
                pendingTitle = pendingTitlePresent ? std::string(rest.substr(comma + 1))
                                                   : std::string();
                havePending = true;
            }
            // #EXTM3U, #UTF8, #EXT-X-NS-CLOUD:, and anything unknown: ignored.
            continue;
        }

        std::string_view ref = line;
        // Winamp strips an `ASF ` prefix from a media line (M3ULoader).
        if (ref.size() > 4 && ref.compare(0, 4, "ASF ") == 0) {
            ref.remove_prefix(4);
        }
        std::string filename = resolveMediaRef(ref, opts.baseDir);
        Track track;
        track.filename = std::move(filename);
        if (havePending) {
            track.title = pendingTitlePresent ? pendingTitle
                                              : titleFromFilename(track.filename);
            track.lengthMs = pendingLengthMs;
        } else {
            track.title = titleFromFilename(track.filename);
            track.lengthMs = 0;
        }
        havePending = false;
        pendingTitlePresent = false;
        pendingTitle.clear();
        pendingLengthMs = 0;
        if (!track.filename.empty()) {
            tracks.push_back(std::move(track));
        }
        if (tracks.size() >= kMaxTracks) {
            break;
        }
    }
    return tracks;
}

}  // namespace koalaamp::playlist
