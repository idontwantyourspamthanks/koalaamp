// SPDX-License-Identifier: MIT
//
// TitleText — the 5x6 `text.bmp` bitmap-font text path: codepoint folding into
// the uppercase-only glyph set, and the Winamp title-marquee layout.
//
// Behaviour ported from Webamp (MIT) at
// webamp@e6283c0eabe9b1313cd41df16d2875e897719696:
//   * the diacritic fold table is `js/utils.ts`'s `diacriticFold` map
//     (`BC`/`TC`/`UC` constants) — 190 entries for U+00C0..U+017F;
//   * the marquee layout is `js/components/Marquee.tsx`: titles of 31 cells or
//     more are doubled with a `"  ***  "` separator and scroll one cell every
//     `marqueeStep`; shorter titles are padded to 31 cells and never move.
// Attribution in LICENSES.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace koalaamp::ui {

/// `#marquee` is 154 px wide over 5 px cells (Webamp `css/main-window.css`).
inline constexpr int kMarqueeWidth = 154;
/// Webamp wraps/pads at 31 cells (155 px, one column clipped by the viewport).
inline constexpr int kMarqueeCells = 31;
/// The separator interposed between the two halves of a scrolling title.
inline constexpr std::string_view kMarqueeSeparator = "  ***  ";

/// Decodes the next UTF-8 codepoint from `text` starting at `pos`, advancing
/// `pos` past it. Invalid bytes decode as U+FFFD and consume one byte, so the
/// loop always terminates.
char32_t nextCodepoint(std::string_view text, std::size_t& pos);

/// Codepoint count (not byte length) — Webamp measures the title in JS string
/// units, which equals the codepoint count for the BMP text it renders.
std::size_t codepointCount(std::string_view text);

/// Folds `cp` into the uppercase-only `text.bmp` glyph set the way Webamp does:
/// diacritic-fold (Å→A, ß→s), drop combining marks, lower-case (Webamp's CSS
/// looks the glyph up by lower-case code; the font's cells are case-shared).
/// Returns U+FFFD when the codepoint has no glyph (rendered as a blank cell).
char32_t foldForFont(char32_t cp);

/// True when the title is long enough to scroll (Webamp `sx`: length >= 31).
bool marqueeScrolls(std::string_view title);

/// The string the marquee actually shows: the doubled form with the separator
/// for long titles, otherwise the title padded to 31 cells with spaces.
std::string marqueeDisplay(std::string_view title);

/// Scroll offset in pixels for animation step `step` (one step per 220 ms,
/// Webamp's marquee interval). Returns 0 for titles that fit.
int marqueeScrollPx(std::string_view title, int step);

}  // namespace koalaamp::ui
