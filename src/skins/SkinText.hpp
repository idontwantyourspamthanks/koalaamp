// SPDX-License-Identifier: MIT
//
// SkinText — the lenient text-config parsers of a classic skin (docs/spec.md
// §5.1, docs/research/skins.md §1.3): `viscolor.txt`, `pledit.txt`,
// `region.txt`, plus the generic INI reader they share.
//
// Leniency rules are ported from Webamp (MIT, pinned at
// webamp@e6283c0eabe9b1313cd41df16d2875e897719696):
//   * `parseIni`      — utils.ts `parseIni` (lower-cased sections/keys, value
//                       truncated at a second `=`, surrounding quotes stripped).
//   * `parseVisColor` — utils.ts `parseViscolors` (invalid lines are skipped and
//                       the *remaining* lines compact upward; short files keep
//                       the built-in defaults for the tail). SE-1 additionally
//                       accepts a leading `0x`/`#` per component and `//`
//                       comments, per docs/tickets/SE-1-wsz-loader.md.
//   * `parsePledit`   — skinParserUtils.ts `getPlaylistStyle` (per-key merge
//                       onto the default playlist colours; over-long hex
//                       truncates to 6 digits, `#` is optional).
//   * `parseRegion`   — regionParser.ts (NumPoints/PointList, polygons with
//                       fewer than three points dropped, space *or* comma
//                       separators). SE-1 is deliberately more forgiving than
//                       Webamp where a missing/short NumPoints would drop the
//                       whole section: the point list is then taken as one polygon.
// Each parser is total: malformed input yields an empty/partial value, never an
// exception.
#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace koalaamp::skins {

/// 8-bit RGB triple, deliberately free of Qt so the parsers stay pure.
struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
    bool operator==(const Rgb&) const = default;
};

/// Generic lenient INI: sections -> keys -> values (all names lower-cased).
using IniSections = std::map<std::string, std::map<std::string, std::string>>;
IniSections parseIni(std::string_view text);

/// True when `text` holds at least one non-comment, non-blank line — a file that
/// carries data some parser could have used, rather than only comments/blank
/// lines. Distinguishes "the skin shipped a placeholder, all-comments file"
/// (the classic base skin's REGION.TXT is exactly that: docs with every line
/// commented, and Winamp draws a rectangular window) from "this file has data
/// we failed to understand" — only the latter is worth a warning.
bool hasIniContent(std::string_view text);

/// `viscolor.txt`: exactly 24 colours (spectrum gradient, oscilloscope, peak).
struct VisColor {
    static constexpr std::size_t kCount = 24;
    std::array<Rgb, kCount> colors{};
};
/// Webamp's built-in viscolor fallback (baseSkin.json `colors`).
VisColor defaultVisColor();
/// Parses as many of the 24 slots as the text provides; the rest stay default.
VisColor parseVisColor(std::string_view text);

/// `pledit.txt` `[Text]` colours + playlist font.
struct PleditColors {
    Rgb normal{0, 255, 0};
    Rgb current{255, 255, 255};
    Rgb normalBg{0, 0, 0};
    Rgb selectedBg{0, 0, 255};
    Rgb mbFg{0, 255, 0};
    Rgb mbBg{0, 0, 0};
    std::string font{"Arial"};
    bool operator==(const PleditColors&) const = default;
};
/// Webamp's built-in playlist style (baseSkin.json `playlistStyle`).
PleditColors defaultPleditColors();
PleditColors parsePledit(std::string_view text);

struct RegionPoint {
    int x = 0;
    int y = 0;
    bool operator==(const RegionPoint&) const = default;
};

struct RegionPolygon {
    std::vector<RegionPoint> points;
};

/// `region.txt`: section name (lower-cased) -> visible-area polygons.
struct Region {
    std::map<std::string, std::vector<RegionPolygon>> sections;

    bool empty() const { return sections.empty(); }
    std::size_t polygonCount() const;
    /// Total number of vertices across every polygon.
    std::size_t pointCount() const;
};
Region parseRegion(std::string_view text);

/// Parses a bare colour value: `#RRGGBB`, `RRGGBB`, `0xRRGGBB` (over-long hex
/// truncates to 6 digits) or a decimal/whitespace `R,G,B` triple. Returns
/// nullopt when nothing usable is present.
std::optional<Rgb> parseRgbValue(std::string_view value);

/// Parses a single decimal/hex component (used by viscolor's line parser).
std::optional<int> parseColorComponent(std::string_view token);

}  // namespace koalaamp::skins
