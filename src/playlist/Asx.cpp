// SPDX-License-Identifier: MIT
//
// ASX / WPL — read-only (the ticket allows "ASX/WPL read (write optional)";
// we record the choice here: neither format is a round-trip target, and both
// are Windows-Media-shaped XML that Winamp only *reads* to pull media refs).
//
//   ASX  — `<ASX><Entry><Title>…</Title><Ref href="…"/>`
//          `<Duration value="H:MM:SS.mmm"/>`
//   ASXv2 is the same shape; `.wax`/`.wvx` are ASX aliases.
//   WPL  — `<smil><body><seq><media src="…"/>…`
#include "playlist/Playlist.hpp"

#include "playlist/Util.hpp"
#include "playlist/Xml.hpp"

#include <string>
#include <string_view>

namespace koalaamp::playlist {
namespace {

/// Parses `H:MM:SS.mmm` (or `M:SS`, or bare seconds) into milliseconds.
int parseAsxDuration(std::string_view value) {
    value = trim(value);
    if (value.empty() || value == "-1") {
        return 0;
    }
    long hours = 0;
    long minutes = 0;
    long seconds = 0;
    long millis = 0;
    const auto parts = split(value, ':');
    if (parts.size() == 3) {
        if (!parseLong(parts[0], hours) || !parseLong(parts[1], minutes)) {
            return 0;
        }
        const auto secParts = split(parts[2], '.');
        if (!parseLong(secParts[0], seconds)) {
            return 0;
        }
        if (secParts.size() >= 2) {
            long f = 0;
            std::size_t digits = 0;
            bool ok = true;
            for (const char c : secParts[1]) {
                if (c < '0' || c > '9') {
                    ok = false;
                    break;
                }
                if (digits < 3) {
                    f = f * 10 + (c - '0');
                    ++digits;
                }
            }
            if (ok) {
                while (digits < 3) {
                    f *= 10;
                    ++digits;
                }
                millis = f;
            }
        }
    } else if (parts.size() == 2) {
        if (!parseLong(parts[0], minutes)) {
            return 0;
        }
        if (!parseLong(parts[1], seconds)) {
            return 0;
        }
    } else {
        if (!parseLong(parts[0], seconds)) {
            return 0;
        }
    }
    if (hours < 0 || minutes < 0 || seconds < 0 || millis < 0) {
        return 0;
    }
    const long total = ((hours * 60 + minutes) * 60 + seconds) * 1000 + millis;
    if (total > 2000000000L) {
        return 0;
    }
    return static_cast<int>(total);
}

}  // namespace

std::vector<Track> readAsx(std::string_view bytes, const ReadOptions& opts) {
    const std::string text = toValidUtf8(stripBom(bytes));
    const xml::Node root = xml::parse(text);
    if (root.name.empty()) {
        return {};
    }
    std::vector<Track> tracks;

    // ASX groups media under `<Entry>`; each Entry carries one `<Title>`, one
    // `<Duration>` and one or more `<Ref href=…>` references.
    for (const auto& owned : root.children) {
        if (!owned || !equalsIgnoreCase(owned->name, "entry")) {
            continue;
        }
        const xml::Node& entry = *owned;
        std::string title;
        if (const xml::Node* titleNode = xml::child(entry, "title")) {
            title = std::string(trim(titleNode->text));
        }
        int lengthMs = 0;
        if (const xml::Node* duration = xml::child(entry, "duration")) {
            if (const std::string* value = xml::attribute(*duration, "value")) {
                lengthMs = parseAsxDuration(*value);
            }
        }
        for (const auto& refOwned : entry.children) {
            if (!refOwned || !equalsIgnoreCase(refOwned->name, "ref")) {
                continue;
            }
            const std::string* href = xml::attribute(*refOwned, "href");
            if (href == nullptr) {
                continue;
            }
            Track track;
            track.filename = resolveMediaRef(stripFileScheme(*href), opts.baseDir);
            if (track.filename.empty()) {
                continue;
            }
            track.title = title.empty() ? titleFromFilename(track.filename) : title;
            track.lengthMs = lengthMs;
            tracks.push_back(std::move(track));
            if (tracks.size() >= kMaxTracks) {
                break;
            }
        }
        if (tracks.size() >= kMaxTracks) {
            break;
        }
    }
    return tracks;
}

std::vector<Track> readWpl(std::string_view bytes, const ReadOptions& opts) {
    const std::string text = toValidUtf8(stripBom(bytes));
    const xml::Node root = xml::parse(text);
    if (root.name.empty()) {
        return {};
    }
    std::vector<Track> tracks;
    // Walk the whole tree for `media` elements carrying a `src`.
    std::vector<const xml::Node*> pending{&root};
    while (!pending.empty() && tracks.size() < kMaxTracks) {
        const xml::Node* node = pending.back();
        pending.pop_back();
        for (const auto& owned : node->children) {
            if (!owned) {
                continue;
            }
            if (equalsIgnoreCase(owned->name, "media")) {
                const std::string* src = xml::attribute(*owned, "src");
                if (src != nullptr) {
                    const std::string_view ref = stripFileScheme(*src);
                    Track track;
                    track.filename = resolveMediaRef(ref, opts.baseDir);
                    if (!track.filename.empty()) {
                        if (const xml::Node* title = xml::child(*owned, "title")) {
                            track.title = std::string(trim(title->text));
                        }
                        if (track.title.empty()) {
                            track.title = titleFromFilename(track.filename);
                        }
                        tracks.push_back(std::move(track));
                    }
                }
            }
            pending.push_back(owned.get());
        }
    }
    return tracks;
}

}  // namespace koalaamp::playlist
