// SPDX-License-Identifier: MIT
//
// GenRenderer — the classic skin's "general purpose" (`gen.bmp`) window frame:
// the tiled chrome Winamp gives plugin windows (AVS, Media Library), and the
// surface VZ-1's visualizer window is built on.
//
// Geometry comes from the sprite table (`skins/SkinSprites.hpp` `kSpritesGEN`,
// `gen.bmp` 194x109 per docs/spikes/fe3-wsz-render.md §5) and the classic gen
// window layout (winampskins.neocities.org "Gen" windows; the public skinning
// notes at nikotheshadow.free.fr). The title bar is six 25x20 pieces, active row
// then inactive, each pair split by a 1 px gap:
//   left corner, left transition, title plate, right transition, stretch tile,
//   right corner (the idle close button is drawn into that last piece).
// The stretch tile runs the full width between the corners. The title plate
// (tiled to the text) sits in the middle and interrupts it, with the two
// transition pieces easing the join. Side borders are 11 px / 8 px, tiled in
// 29 px steps and closed by their 24 px `*_BOTTOM` caps. The bottom bar is a
// 125 px left corner, a 25 px tile, and a 125 px right corner (the resize grip).
// The 25 px / 29 px steps are the ones docs/research/skins.md §1.4 cites from
// Webamp (MIT, `js/constants.ts`). Attribution in LICENSES.md.
//
// The title text is the sheet's own alphabet: two 7 px rows under the frame
// pieces (active, then inactive), variable width, split by a solid delimiter
// colour. A sheet with no alphabet falls back to `text.bmp`, still centred,
// with the font's cell background left transparent so the letters sit on the
// plate instead of in a box.
//
// `gen.bmp` is optional (`skins/SkinSet.cpp`) and most skins omit it. Those
// get the playlist window's own chrome (`pledit.bmp`: title corners and tile,
// side borders, the plain lower edge of the bottom band plus its grip) so the
// visualizer matches its siblings. A skin with neither sheet still gets the
// palette frame below.
//
// Rendering rules (docs/spec.md §5.2): exact integer blits, composed at 1x and
// integer-upscaled by `scale` (never fractional).
//
// docs/spec.md §5.2-5.3, §6.1; docs/tickets/VZ-1-visualizer.md task 1.
#pragma once

#include <string>

#include <QImage>
#include <QRect>
#include <QSize>

#include "skins/Skin.hpp"
#include "skins/SkinText.hpp"

namespace koalaamp::ui {

/// Reference size of a gen window (the decoded `gen.bmp` metrics).
inline constexpr int kGenWidth = 194;
inline constexpr int kGenHeight = 109;
/// Title bar height (the title sprites are 25x20).
inline constexpr int kGenTitleHeight = 20;
/// Bottom bar height (the bottom tiles are 25x14).
inline constexpr int kGenBottomHeight = 14;
/// Side border widths (`GEN_MIDDLE_LEFT` 11 px, `GEN_MIDDLE_RIGHT` 8 px).
inline constexpr int kGenLeftBorder = 11;
inline constexpr int kGenRightBorder = 8;
/// Horizontal / vertical tiling steps of the frame.
inline constexpr int kGenSegment = 25;
inline constexpr int kGenSegmentHeight = 29;
/// The close box. The idle button is painted into the right title piece; this
/// rect is its hit target and where `GEN_CLOSE_SELECTED` lands while held.
/// In that 25x20 piece the 9x9 button has 3 px above it and 2 px to its right.
inline constexpr int kGenCloseSize = 9;
inline constexpr int kGenCloseInsetTop = 3;
inline constexpr int kGenCloseInsetRight = 2;
/// Gap kept between the title text and the close box.
inline constexpr int kGenCloseMargin = 2;
/// Each bottom corner sprite (`GEN_BOTTOM_LEFT` / `GEN_BOTTOM_RIGHT`).
inline constexpr int kGenBottomCorner = 125;

/// Where each part of a gen window sits, in 1x skin pixels. The client rect is
/// what a plugin (here: the GL canvas) is expected to fill.
struct GenLayout {
    QRect title;
    QRect closeButton;
    QRect leftBorder;
    QRect rightBorder;
    QRect bottom;
    QRect client;
};

GenLayout genLayout(const QSize& contentSize);

/// The nearest valid gen-window size at or above `wanted`: at least the
/// reference size, a whole number of tiling steps larger, and wide enough for
/// both 125 px bottom corners to sit side by side.
QSize genSnapSize(const QSize& wanted);

/// The composing frame: title bar, borders, bottom bar, close box and the
/// client area. `active` picks the skin's selected title-bar sprites. The title
/// is centred: the sheet's own gen alphabet when it has one, otherwise the 5x6
/// `text.bmp` font (uppercase-folded), otherwise the fallback font. `pledit` is
/// the skin's playlist palette, used when the skin ships neither `gen.bmp` nor
/// a usable `pledit.bmp`.
/// `closePressed` overlays the held close sprite. Returns a
/// `genSnapSize(contentSize) * scale` image.
QImage renderGenWindow(const skins::Skin& skin, const skins::PleditColors* pledit,
                       const QSize& contentSize, const std::string& title, bool active,
                       int scale = 1, bool closePressed = false);

/// True when the fallback frame would be used (the skin ships no `gen.bmp`).
bool genUsesFallback(const skins::Skin& skin);

}  // namespace koalaamp::ui
