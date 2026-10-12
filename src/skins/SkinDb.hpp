// SPDX-License-Identifier: MIT
//
// SkinDb — the "which skin / where were its windows" state model
// (docs/tickets/SE-1-wsz-loader.md): the current skin, a capped most-recently-
// used list, and per-skin window layout (position + shade state) so a skin is
// restored the way the user left it.
//
// Persistence is a small line-oriented text format (no third-party parser).
// Every read path is total: malformed lines are ignored, a missing file is not
// an error. Window dragging itself belongs to the SE-2 windows; the loader-side
// entry point is `noteLoaded()`.
#pragma once

#include <cstddef>
#include <filesystem>
#include <istream>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace koalaamp::skins {

struct WindowPlacement {
    int x = 0;
    int y = 0;
    bool operator==(const WindowPlacement&) const = default;
};

/// Per-skin window positions, windowshade states and visibility
/// (docs/spec.md §5.3). Visibility defaults true so records written before the
/// flags existed restore all windows (SE-4).
struct SkinLayoutState {
    WindowPlacement main;
    WindowPlacement equalizer;
    WindowPlacement playlist;
    int playlistWidth = 275;
    int playlistHeight = 0;
    bool mainShade = false;
    bool eqShade = false;
    bool playlistShade = false;
    bool mainVisible = true;
    bool eqVisible = true;
    bool playlistVisible = true;
    bool operator==(const SkinLayoutState&) const = default;
};

class SkinDb {
public:
    static constexpr std::size_t kMaxRecent = 20;

    /// Canonical identity for a skin path (weakly canonical, absolute fallback).
    static std::string keyFor(const std::filesystem::path& skin);

    /// Records a successful load: becomes `current` and moves to the front of
    /// `recent` (deduplicated, capped at kMaxRecent).
    void noteLoaded(const std::filesystem::path& skin);
    void setCurrent(const std::filesystem::path& skin) { m_current = skin; }

    const std::filesystem::path& current() const { return m_current; }
    const std::vector<std::filesystem::path>& recent() const { return m_recent; }

    /// Drops a skin from `recent` (and its layout). Returns how many entries went.
    std::size_t forget(const std::filesystem::path& skin);

    /// Layout record for a skin, created on first use.
    SkinLayoutState& layoutFor(const std::filesystem::path& skin);
    /// Layout record for a skin, or nullptr when never seen.
    const SkinLayoutState* layout(const std::filesystem::path& skin) const;

    void clear();

    void save(std::ostream& out) const;
    void load(std::istream& in);
    bool saveTo(const std::filesystem::path& file) const;
    bool loadFrom(const std::filesystem::path& file);

    /// `key -> layout` view, for tests and diagnostics.
    const std::map<std::string, SkinLayoutState>& layouts() const { return m_layouts; }

private:
    std::filesystem::path m_current;
    std::vector<std::filesystem::path> m_recent;
    std::map<std::string, SkinLayoutState> m_layouts;
};

}  // namespace koalaamp::skins
