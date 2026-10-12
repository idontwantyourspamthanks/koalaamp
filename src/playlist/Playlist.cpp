// SPDX-License-Identifier: MIT
//
// Playlist — format dispatch, path canonicalisation, the Media Library index
// (`playlists.xml`) and autosave. See Playlist.hpp for the contract.
#include "playlist/Playlist.hpp"

#include "playlist/Util.hpp"
#include "playlist/Xml.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <system_error>

namespace koalaamp::playlist {
namespace {

bool hasUrlScheme(std::string_view s) { return s.find("://") != std::string_view::npos; }

bool isUncPath(std::string_view s) { return s.size() >= 2 && s[0] == '/' && s[1] == '/'; }

bool isWindowsDrivePath(std::string_view s) {
    return s.size() >= 3 && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) &&
           s[1] == ':' && (s[2] == '\\' || s[2] == '/');
}

/// Sniffs a track-playlist format from content when the extension is unknown.
Format sniffFormat(std::string_view bytes) {
    const std::string_view body = stripBom(bytes);
    const std::string_view lead = trim(body);
    if (lead.empty()) {
        return Format::Unknown;
    }
    if (lead[0] == '[') {
        return Format::Pls;
    }
    if (lead.rfind("#EXTM3U", 0) == 0) {
        return stripBom(bytes).size() != bytes.size() ? Format::M3u8 : Format::M3u;
    }
    if (lead[0] == '<') {
        const xml::Node root = xml::parse(body);
        if (equalsIgnoreCase(root.name, "playlist")) {
            return Format::Xspf;
        }
        if (equalsIgnoreCase(root.name, "asx")) {
            return Format::Asx;
        }
        if (equalsIgnoreCase(root.name, "smil")) {
            return Format::Wpl;
        }
        if (equalsIgnoreCase(root.name, "winampxml") || equalsIgnoreCase(root.name, "wasabixml")) {
            return Format::B4s;
        }
        return Format::Unknown;
    }
    return Format::M3u;
}

}  // namespace

// --- path handling ----------------------------------------------------------

std::string canonicalKey(const std::filesystem::path& path) {
    // generic_string(): forward slashes on every platform, so the key is a
    // stable identity and a joined path never inherits native separators
    // (path::string() renders '\\' on Windows — that rewrote playlist entries
    // and failed the Windows round-trip).
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec && !canonical.empty()) {
        return canonical.generic_string();
    }
    const auto absolute = std::filesystem::absolute(path, ec);
    if (!ec && !absolute.empty()) {
        return absolute.generic_string();
    }
    return path.generic_string();
}

std::string resolveMediaRef(std::string_view ref, const std::filesystem::path& baseDir) {
    ref = trim(ref);
    if (ref.empty()) {
        return {};
    }
    // A playlist entry is opaque TEXT: an M3U written on Linux carries POSIX
    // separators and must survive a Windows read unchanged (and vice versa), so
    // an entry that names a location of its own is passed through verbatim —
    // never routed through the platform's path rendering, which rewrites
    // separators (path::string() gives '\\' on Windows) and bolts on a drive.
    if (hasUrlScheme(ref) || isUncPath(ref) || isWindowsDrivePath(ref)) {
        return std::string(ref);
    }
    // "Rooted", not is_absolute(): on Windows a path with a root-directory but
    // no root-name ("/music/x.mp3", "\music\x.mp3") is NOT absolute, and
    // `baseDir / "/music/x.mp3"` then REPLACES all but the drive, turning the
    // entry into "D:/music/x.mp3" (exactly what Windows CI caught). Rooted
    // entries are the author's text; only genuinely relative entries need
    // resolving, and those are the only ones canonicalKey touches.
    const std::filesystem::path p(ref);
    if (p.has_root_name() || p.has_root_directory()) {
        return std::string(ref);
    }
    if (baseDir.empty()) {
        return std::string(ref);
    }
    return canonicalKey(baseDir / p);
}

std::string titleFromFilename(std::string_view filename) {
    std::string_view part = filePart(filename);
    // Strip the extension: the final dot, when it is not the leading char.
    const std::size_t dot = part.find_last_of('.');
    if (dot != std::string_view::npos && dot != 0) {
        part = part.substr(0, dot);
    }
    return std::string(part);
}

// --- format dispatch --------------------------------------------------------

Format formatForExtension(std::string_view ext) {
    if (!ext.empty() && ext.front() == '.') {
        ext.remove_prefix(1);
    }
    if (equalsIgnoreCase(ext, "m3u")) {
        return Format::M3u;
    }
    if (equalsIgnoreCase(ext, "m3u8")) {
        return Format::M3u8;
    }
    if (equalsIgnoreCase(ext, "pls")) {
        return Format::Pls;
    }
    if (equalsIgnoreCase(ext, "b4s")) {
        return Format::B4s;
    }
    if (equalsIgnoreCase(ext, "xspf")) {
        return Format::Xspf;
    }
    if (equalsIgnoreCase(ext, "asx") || equalsIgnoreCase(ext, "wax") || equalsIgnoreCase(ext, "wvx")) {
        return Format::Asx;
    }
    if (equalsIgnoreCase(ext, "wpl")) {
        return Format::Wpl;
    }
    return Format::Unknown;
}

const std::vector<std::string>& supportedExtensions() {
    static const std::vector<std::string> extensions = {
        ".m3u", ".m3u8", ".pls", ".b4s", ".xspf", ".asx", ".wax", ".wvx", ".wpl",
    };
    return extensions;
}

std::string extensionForFormat(Format format) {
    switch (format) {
        case Format::M3u: return ".m3u";
        case Format::M3u8: return ".m3u8";
        case Format::Pls: return ".pls";
        case Format::B4s: return ".b4s";
        case Format::Xspf: return ".xspf";
        case Format::Asx: return ".asx";
        case Format::Wpl: return ".wpl";
        case Format::Unknown: break;
    }
    return {};
}

Playlist readFile(const std::filesystem::path& file) {
    Playlist playlist;
    const auto bytes = readFileBytes(file);
    if (!bytes.has_value()) {
        return playlist;
    }
    Format format = formatForExtension(file.extension().string());
    if (format == Format::Unknown) {
        format = sniffFormat(*bytes);
    }
    playlist.format = format;
    ReadOptions opts;
    opts.baseDir = file.parent_path();
    opts.utf8 = format == Format::M3u8;

    switch (format) {
        case Format::M3u:
        case Format::M3u8:
            playlist.tracks = readM3u(*bytes, opts);
            break;
        case Format::Pls:
            playlist.tracks = readPls(*bytes, opts);
            break;
        case Format::B4s:
            playlist.tracks = readB4s(*bytes, opts);
            break;
        case Format::Xspf:
            playlist.tracks = readXspf(*bytes, opts);
            break;
        case Format::Asx:
            playlist.tracks = readAsx(*bytes, opts);
            break;
        case Format::Wpl:
            playlist.tracks = readWpl(*bytes, opts);
            break;
        case Format::Unknown:
            break;
    }
    return playlist;
}

bool writeFile(const std::filesystem::path& file, const std::vector<Track>& tracks, Format format) {
    std::string bytes;
    switch (format) {
        case Format::M3u: bytes = writeM3u(tracks, false); break;
        case Format::M3u8: bytes = writeM3u(tracks, true); break;
        case Format::Pls: bytes = writePls(tracks); break;
        case Format::B4s: bytes = writeB4s(tracks); break;
        case Format::Xspf: bytes = writeXspf(tracks); break;
        // ASX/WPL are read-only (see Asx.cpp).
        case Format::Asx:
        case Format::Wpl:
        case Format::Unknown:
            return false;
    }
    return writeFileAtomic(file, bytes);
}

// --- Media Library index ----------------------------------------------------

std::string newGuid() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    const std::uint64_t hi = rng();
    const std::uint64_t lo = rng();
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "{%08X-%04X-%04X-%04X-%012llX}",
                  static_cast<unsigned>((hi >> 32) & 0xFFFFFFFFu),
                  static_cast<unsigned>((hi >> 16) & 0xFFFFu),
                  static_cast<unsigned>(0x4000u | ((hi) & 0x0FFFu)),
                  static_cast<unsigned>(0x8000u | ((lo >> 48) & 0x3FFFu)),
                  static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFull));
    return buffer;
}

std::string writeIndex(const std::vector<IndexEntry>& entries) {
    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<Winamp:Playlists>\n";
    for (const IndexEntry& entry : entries) {
        out += "<playlist filename=\"";
        out += xmlEscape(entry.filename);
        out += "\" title=\"";
        out += xmlEscape(entry.title);
        out += "\" songs=\"";
        out += std::to_string(entry.songs);
        out += "\" seconds=\"";
        out += std::to_string(entry.seconds);
        out += "\" id=\"";
        out += xmlEscape(entry.id);
        out += "\"/>\n";
    }
    out += "</Winamp:Playlists>\n";
    return out;
}

std::vector<IndexEntry> readIndex(std::string_view bytes) {
    const std::string text = toValidUtf8(stripBom(bytes));
    const xml::Node root = xml::parse(text);
    if (root.name.empty()) {
        return {};
    }
    // Root is `Winamp:Playlists` (or `playlists`); accept either and tolerate a
    // nested wrapper by scanning descendants if the direct children don't match.
    auto collect = [](const xml::Node& parent, std::vector<IndexEntry>& out) {
        for (const auto& owned : parent.children) {
            if (!owned || !equalsIgnoreCase(owned->name, "playlist")) {
                continue;
            }
            IndexEntry entry;
            if (const std::string* filename = xml::attribute(*owned, "filename")) {
                entry.filename = *filename;
            }
            if (const std::string* title = xml::attribute(*owned, "title")) {
                entry.title = *title;
            }
            long value = 0;
            if (const std::string* songs = xml::attribute(*owned, "songs");
                songs != nullptr && parseLong(trim(*songs), value)) {
                entry.songs = value;
            }
            if (const std::string* seconds = xml::attribute(*owned, "seconds");
                seconds != nullptr && parseLong(trim(*seconds), value)) {
                entry.seconds = value;
            }
            if (const std::string* id = xml::attribute(*owned, "id")) {
                entry.id = *id;
            } else if (const std::string* itunes = xml::attribute(*owned, "iTunesID")) {
                entry.id = *itunes;
            }
            if (entry.filename.empty()) {
                continue;
            }
            out.push_back(std::move(entry));
        }
    };
    std::vector<IndexEntry> entries;
    collect(root, entries);
    if (entries.empty()) {
        // Flatten: find the first `playlists`-ish container, else walk all.
        std::vector<const xml::Node*> pending{&root};
        while (!pending.empty()) {
            const xml::Node* node = pending.back();
            pending.pop_back();
            collect(*node, entries);
            for (const auto& owned : node->children) {
                if (owned) {
                    pending.push_back(owned.get());
                }
            }
            if (!entries.empty()) {
                break;
            }
        }
    }
    return entries;
}

// --- autosave ---------------------------------------------------------------

std::filesystem::path autosavePlaylistPath(const std::filesystem::path& dir) {
    return dir / "playlist.m3u8";
}

std::filesystem::path autosaveStatePath(const std::filesystem::path& dir) {
    return dir / "playlist.state";
}

bool saveAutosave(const std::filesystem::path& dir, const AutosaveState& state) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    bool ok = writeFileAtomic(autosavePlaylistPath(dir), writeM3u(state.tracks, /*utf8=*/true));

    std::string sidecar = "koalaamp-playlist-state 1\n";
    sidecar += "current ";
    sidecar += std::to_string(state.currentIndex);
    sidecar += "\nqueue";
    for (const std::size_t index : state.queue) {
        sidecar += ' ';
        sidecar += std::to_string(index);
    }
    sidecar += '\n';
    ok = writeFileAtomic(autosaveStatePath(dir), sidecar) && ok;
    return ok;
}

std::optional<AutosaveState> loadAutosave(const std::filesystem::path& dir) {
    const auto playlistBytes = readFileBytes(autosavePlaylistPath(dir));
    if (!playlistBytes.has_value()) {
        return std::nullopt;
    }
    AutosaveState state;
    ReadOptions opts;
    opts.baseDir = dir;
    opts.utf8 = true;
    state.tracks = readM3u(*playlistBytes, opts);
    state.currentIndex = -1;

    const auto sidecar = readFileBytes(autosaveStatePath(dir));
    if (sidecar.has_value()) {
        for (std::string_view raw : splitLines(*sidecar)) {
            const std::string_view line = trim(raw);
            if (line.rfind("current", 0) == 0) {
                long index = -1;
                if (parseLong(trim(line.substr(7)), index)) {
                    state.currentIndex =
                        index >= 0 && static_cast<std::size_t>(index) < state.tracks.size()
                            ? static_cast<int>(index)
                            : -1;
                }
            } else if (line.rfind("queue", 0) == 0) {
                state.queue.clear();
                for (std::string_view token : split(trim(line.substr(5)), ' ')) {
                    long index = -1;
                    if (!parseLong(trim(token), index) || index < 0) {
                        continue;
                    }
                    const auto pos = static_cast<std::size_t>(index);
                    if (pos < state.tracks.size()) {
                        state.queue.push_back(pos);
                    }
                }
            }
        }
    }
    return state;
}

}  // namespace koalaamp::playlist
