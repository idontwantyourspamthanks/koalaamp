// SPDX-License-Identifier: MIT
#include "skins/SkinDb.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace koalaamp::skins {
namespace {

constexpr char kMagic[] = "koalaamp-skindb 1";

bool hasControlChar(const std::string& s) {
    return std::any_of(s.begin(), s.end(),
                       [](unsigned char c) { return c < 0x20 || c == 0x7F; });
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto tab = line.find('\t', start);
        parts.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) {
            break;
        }
        start = tab + 1;
    }
    return parts;
}

bool parseInt(const std::string& token, int& out) {
    try {
        std::size_t used = 0;
        const int value = std::stoi(token, &used);
        if (used != token.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

std::string SkinDb::keyFor(const std::filesystem::path& skin) {
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(skin, ec);
    if (!ec && !canonical.empty()) {
        return canonical.string();
    }
    const auto absolute = std::filesystem::absolute(skin, ec);
    if (!ec && !absolute.empty()) {
        return absolute.string();
    }
    return skin.string();
}

void SkinDb::noteLoaded(const std::filesystem::path& skin) {
    m_current = skin;
    const std::string key = keyFor(skin);
    m_recent.erase(std::remove_if(m_recent.begin(), m_recent.end(),
                                  [&key](const std::filesystem::path& p) {
                                      return keyFor(p) == key;
                                  }),
                   m_recent.end());
    m_recent.insert(m_recent.begin(), skin);
    if (m_recent.size() > kMaxRecent) {
        m_recent.resize(kMaxRecent);
    }
}

std::size_t SkinDb::forget(const std::filesystem::path& skin) {
    const std::string key = keyFor(skin);
    const auto before = m_recent.size();
    m_recent.erase(std::remove_if(m_recent.begin(), m_recent.end(),
                                  [&key](const std::filesystem::path& p) {
                                      return keyFor(p) == key;
                                  }),
                   m_recent.end());
    m_layouts.erase(key);
    if (keyFor(m_current) == key) {
        m_current.clear();
    }
    return before - m_recent.size();
}

SkinLayoutState& SkinDb::layoutFor(const std::filesystem::path& skin) {
    return m_layouts[keyFor(skin)];
}

const SkinLayoutState* SkinDb::layout(const std::filesystem::path& skin) const {
    const auto it = m_layouts.find(keyFor(skin));
    return it == m_layouts.end() ? nullptr : &it->second;
}

void SkinDb::clear() {
    m_current.clear();
    m_recent.clear();
    m_layouts.clear();
}

void SkinDb::save(std::ostream& out) const {
    out << kMagic << '\n';
    const auto writePath = [&out](const char* tag, const std::filesystem::path& p) {
        const std::string s = p.string();
        if (s.empty() || hasControlChar(s)) {
            return;  // never emit a line we cannot read back
        }
        out << tag << '\t' << s << '\n';
    };
    writePath("current", m_current);
    for (const auto& p : m_recent) {
        writePath("recent", p);
    }
    for (const auto& [key, state] : m_layouts) {
        if (hasControlChar(key)) {
            continue;
        }
        const int flags = (state.mainShade ? 1 : 0) | (state.eqShade ? 2 : 0) |
                          (state.playlistShade ? 4 : 0) | (state.mainVisible ? 0 : 8) |
                          (state.eqVisible ? 0 : 16) | (state.playlistVisible ? 0 : 32);
        out << "layout\t" << state.main.x << '\t' << state.main.y << '\t' << state.equalizer.x
            << '\t' << state.equalizer.y << '\t' << state.playlist.x << '\t' << state.playlist.y
            << '\t' << state.playlistWidth << '\t' << state.playlistHeight << '\t' << flags
            << '\t' << key << '\n';
        if (state.visualizer.has_value()) {
            // A separate record keeps the `layout` line above byte-identical in
            // meaning for every reader that predates the visualizer.
            out << "layout2\t" << state.visualizer->x << '\t' << state.visualizer->y << '\t'
                << (state.visualizerVisible ? 0 : 1) << '\t' << key << '\n';
        }
    }
}

namespace {

/// The stored key is the tab-joined *tail* of a record line, not a single field:
/// older writers emitted keys that may themselves contain tabs, so readers
/// reassemble them from a start index to the end.
std::string keyFrom(const std::vector<std::string>& parts, std::size_t start) {
    std::string key;
    for (std::size_t i = start; i < parts.size(); ++i) {
        if (i > start) {
            key += '\t';
        }
        key += parts[i];
    }
    return key;
}

}  // namespace

void SkinDb::load(std::istream& in) {
    clear();
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        auto parts = splitTabs(line);
        if (parts.size() < 2) {
            continue;  // magic / junk
        }
        if (parts[0] == "current") {
            m_current = parts[1];
        } else if (parts[0] == "recent") {
            if (m_recent.size() < kMaxRecent) {
                m_recent.emplace_back(parts[1]);
            }
        } else if (parts[0] == "layout" && parts.size() >= 11) {
            int values[8] = {0, 0, 0, 0, 0, 0, 275, 0};
            bool ok = true;
            for (int i = 0; i < 8 && ok; ++i) {
                ok = parseInt(parts[static_cast<std::size_t>(i) + 1], values[i]);
            }
            if (!ok) {
                continue;
            }
            SkinLayoutState state;
            state.main = {values[0], values[1]};
            state.equalizer = {values[2], values[3]};
            state.playlist = {values[4], values[5]};
            state.playlistWidth = values[6];
            state.playlistHeight = values[7];
            int parsedFlags = 0;
            if (!parseInt(parts[9], parsedFlags)) {
                continue;
            }
            state.mainShade = (parsedFlags & 1) != 0;
            state.eqShade = (parsedFlags & 2) != 0;
            state.playlistShade = (parsedFlags & 4) != 0;
            // Older records (flags <= 7) predate the visibility bits: the
            // defaults stay visible (the pre-SE-4 behaviour).
            state.mainVisible = (parsedFlags & 8) == 0;
            state.eqVisible = (parsedFlags & 16) == 0;
            state.playlistVisible = (parsedFlags & 32) == 0;
            const std::string key = keyFrom(parts, 10);
            // Canonicalise the stored key: the writer stores keyFor(skin)
            // (weakly-canonical), and layout() looks up by keyFor(skin) too —
            // but a record line can carry a relative or rooted path that
            // canonicalises differently per platform (e.g. "/skin.wsz" is
            // itself on POSIX but resolves against the current drive on
            // Windows). Canonicalising here keeps load() self-consistent with
            // the writer/reader regardless of where the file was written.
            m_layouts[keyFor(key)] = state;
        } else if (parts[0] == "layout2" && parts.size() >= 5) {
            // VZ-1's visualizer placement, kept in its own record: the classic
            // `layout` line's key is positional (the tab-joined tail from index
            // 10), so widening it would silently change the meaning of every
            // existing line. A reader that predates this tag ignores the line,
            // and a file with no visualizer simply has no line.
            int x = 0;
            int y = 0;
            int hidden = 0;
            if (!parseInt(parts[1], x) || !parseInt(parts[2], y) || !parseInt(parts[3], hidden)) {
                continue;
            }
            // Decorate the record the `layout` line created; never create one. A
            // stray or hand-edited `layout2` must not fabricate an all-zero layout
            // for the classic three, which would move them to (0,0) on restore.
            const auto found = m_layouts.find(keyFor(keyFrom(parts, 4)));
            if (found != m_layouts.end()) {
                found->second.visualizer = WindowPlacement{x, y};
                found->second.visualizerVisible = hidden == 0;
            }
        }
    }
}

bool SkinDb::saveTo(const std::filesystem::path& file) const {
    std::error_code ec;
    if (const auto parent = file.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    save(out);
    return out.good();
}

bool SkinDb::loadFrom(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    load(in);
    return true;
}

}  // namespace koalaamp::skins
