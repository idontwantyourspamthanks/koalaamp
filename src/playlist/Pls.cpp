// SPDX-License-Identifier: MIT
//
// PLS — the INI-style playlist (docs/research/formats.md §1): a `[playlist]`
// section with `NumberOfEntries`, `Version` and per-track
// `FileN`/`TitleN`/`LengthN` (length in whole seconds; the loader uses
// GetPrivateProfileInt/String, so the format is a case-insensitive INI).
//
// Writer emits `Version=2` (what real Winamp writes; the reader accepts any
// version, including the 0/1 the research lists). An unknown length is written
// as `-1`, the conventional Winamp "unknown".
#include "playlist/Playlist.hpp"

#include "playlist/Util.hpp"

#include <map>
#include <string>
#include <string_view>

namespace koalaamp::playlist {
namespace {

struct PlsEntry {
    bool hasFile = false;
    std::string filename;
    bool hasTitle = false;
    std::string title;
    int lengthMs = 0;
};

/// Parses the trailing index of a key like `Title12` with prefix `title`.
bool keyIndex(std::string_view key, std::string_view prefix, long& index) {
    if (key.size() <= prefix.size() || !equalsIgnoreCase(key.substr(0, prefix.size()), prefix)) {
        return false;
    }
    return parseLong(key.substr(prefix.size()), index) && index >= 1;
}

}  // namespace

std::string writePls(const std::vector<Track>& tracks) {
    std::string out;
    out += "[playlist]\n";
    out += "NumberOfEntries=";
    out += std::to_string(tracks.size());
    out += "\nVersion=2\n";
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const std::size_t n = i + 1;
        const Track& track = tracks[i];
        out += "File";
        out += std::to_string(n);
        out += '=';
        out += track.filename;
        out += '\n';
        out += "Title";
        out += std::to_string(n);
        out += '=';
        out += track.title;
        out += '\n';
        out += "Length";
        out += std::to_string(n);
        out += '=';
        out += track.lengthMs > 0 ? std::to_string(track.lengthMs / 1000) : std::string("-1");
        out += '\n';
    }
    return out;
}

std::vector<Track> readPls(std::string_view bytes, const ReadOptions& opts) {
    const std::string text = toValidUtf8(stripBom(bytes));

    bool inPlaylist = true;  // lenient: keys before/without a section are read
    bool sawAnySection = false;
    std::map<long, PlsEntry> entries;

    for (std::string_view raw : splitLines(text)) {
        const std::string_view line = trim(raw);
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[') {
            sawAnySection = true;
            const std::size_t close = line.find(']');
            const std::string_view section =
                close == std::string_view::npos ? line.substr(1) : line.substr(1, close - 1);
            inPlaylist = equalsIgnoreCase(trim(section), "playlist");
            continue;
        }
        if (sawAnySection && !inPlaylist) {
            continue;
        }
        if (line.front() == ';' || line.front() == '#') {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trim(line.substr(0, eq));
        const std::string_view value = trim(line.substr(eq + 1));

        long index = 0;
        if (keyIndex(key, "file", index)) {
            entries[index].hasFile = true;
            entries[index].filename = std::string(value);
        } else if (keyIndex(key, "title", index)) {
            entries[index].hasTitle = true;
            entries[index].title = std::string(value);
        } else if (keyIndex(key, "length", index)) {
            long seconds = -1;
            parseLong(value, seconds);
            entries[index].lengthMs = seconds > 0 ? static_cast<int>(seconds * 1000) : 0;
        }
        // NumberOfEntries / Version are not needed for a lenient read.
    }

    std::vector<Track> tracks;
    for (auto& [index, entry] : entries) {
        (void)index;
        if (!entry.hasFile || entry.filename.empty()) {
            continue;
        }
        Track track;
        track.filename = resolveMediaRef(entry.filename, opts.baseDir);
        track.title = entry.hasTitle ? entry.title : titleFromFilename(track.filename);
        track.lengthMs = entry.lengthMs;
        tracks.push_back(std::move(track));
        if (tracks.size() >= kMaxTracks) {
            break;
        }
    }
    return tracks;
}

}  // namespace koalaamp::playlist
