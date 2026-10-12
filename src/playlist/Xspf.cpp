// SPDX-License-Identifier: MIT
//
// XSPF — XML Shareable Playlist Format (docs/research/formats.md §1): a
// `<playlist>` with a `<trackList>` of `<track>` elements carrying
// `<location>` (a file URL), `<title>` and `<duration>` (milliseconds).
// Winamp reads and writes it (Src/xspf); we do the same for the fields that
// matter and ignore extensions.
#include "playlist/Playlist.hpp"

#include "playlist/Util.hpp"
#include "playlist/Xml.hpp"

#include <string>

namespace koalaamp::playlist {

std::string writeXspf(const std::vector<Track>& tracks) {
    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n";
    out += "<trackList>\n";
    for (const Track& track : tracks) {
        out += "<track><location>";
        out += xmlEscape(fileUrlFor(track.filename));
        out += "</location><title>";
        out += xmlEscape(track.title);
        out += "</title><duration>";
        out += std::to_string(track.lengthMs);
        out += "</duration></track>\n";
    }
    out += "</trackList>\n";
    out += "</playlist>\n";
    return out;
}

std::vector<Track> readXspf(std::string_view bytes, const ReadOptions& opts) {
    const std::string text = toValidUtf8(stripBom(bytes));
    const xml::Node root = xml::parse(text);
    if (root.name.empty()) {
        return {};
    }
    const xml::Node* trackList = xml::child(root, "trackList");
    if (trackList == nullptr) {
        trackList = xml::descendant(root, "trackList");
    }
    if (trackList == nullptr) {
        return {};
    }

    std::vector<Track> tracks;
    for (const auto& owned : trackList->children) {
        if (!owned || !equalsIgnoreCase(owned->name, "track")) {
            continue;
        }
        const xml::Node& trackNode = *owned;
        const xml::Node* location = xml::child(trackNode, "location");
        if (location == nullptr) {
            continue;
        }
        const std::string_view ref = stripFileScheme(trim(location->text));
        Track track;
        track.filename = resolveMediaRef(ref, opts.baseDir);
        if (track.filename.empty()) {
            continue;
        }
        if (const xml::Node* title = xml::child(trackNode, "title")) {
            track.title = std::string(trim(title->text));
        } else {
            track.title = titleFromFilename(track.filename);
        }
        if (const xml::Node* duration = xml::child(trackNode, "duration")) {
            long ms = 0;
            if (parseLong(trim(duration->text), ms) && ms > 0) {
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
