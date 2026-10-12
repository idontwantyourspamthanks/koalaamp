// SPDX-License-Identifier: MIT
//
// B4S — Winamp 3/5 XML playlist (docs/research/formats.md §1): root `WasabiXML`
// *or* `WinampXML`, a `<playlist>` of `<entry playstring="file://…">` elements
// whose `<name>` and `<length>` children carry the title and milliseconds.
// The loader strips the `file://`/`file:` prefix off `playstring`.
//
// The reader is deliberately tolerant (it finds the `<playlist>` under either
// root, via a plain tolerant XML scan — see Xml.hpp) because the format has no
// single canonical spelling in the wild.
#include "playlist/Playlist.hpp"

#include "playlist/Util.hpp"
#include "playlist/Xml.hpp"

#include <string>

namespace koalaamp::playlist {

std::string writeB4s(const std::vector<Track>& tracks) {
    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<WinampXML>\n";
    out += "<playlist>\n";
    for (const Track& track : tracks) {
        out += "<entry playstring=\"";
        out += xmlEscape(fileUrlFor(track.filename));
        out += "\"><name>";
        out += xmlEscape(track.title);
        out += "</name><length>";
        out += std::to_string(track.lengthMs);
        out += "</length></entry>\n";
    }
    out += "</playlist>\n";
    out += "</WinampXML>\n";
    return out;
}

std::vector<Track> readB4s(std::string_view bytes, const ReadOptions& opts) {
    const std::string text = toValidUtf8(stripBom(bytes));
    const xml::Node root = xml::parse(text);
    if (root.name.empty()) {
        return {};
    }

    const xml::Node* playlist = equalsIgnoreCase(root.name, "WasabiXML") ||
                                        equalsIgnoreCase(root.name, "WinampXML")
                                    ? xml::child(root, "playlist")
                                    : xml::descendant(root, "playlist");
    if (playlist == nullptr) {
        playlist = xml::descendant(root, "playlist");
    }
    if (playlist == nullptr) {
        return {};
    }

    std::vector<Track> tracks;
    for (const auto& owned : playlist->children) {
        if (!owned || !equalsIgnoreCase(owned->name, "entry")) {
            continue;
        }
        const xml::Node& entry = *owned;
        const std::string* playstring = xml::attribute(entry, "playstring");
        if (playstring == nullptr) {
            continue;
        }
        const std::string_view ref = stripFileScheme(*playstring);
        Track track;
        track.filename = resolveMediaRef(ref, opts.baseDir);
        if (track.filename.empty()) {
            continue;
        }
        if (const xml::Node* name = xml::child(entry, "name")) {
            track.title = std::string(trim(name->text));
        } else {
            track.title = titleFromFilename(track.filename);
        }
        if (const xml::Node* length = xml::child(entry, "length")) {
            long ms = 0;
            if (parseLong(trim(length->text), ms) && ms > 0) {
                track.lengthMs = static_cast<int>(ms);
            }
        }
        tracks.push_back(std::move(track));
        if (tracks.size() >= kMaxTracks) {
            break;
        }
    }
    return tracks;
}

}  // namespace koalaamp::playlist
