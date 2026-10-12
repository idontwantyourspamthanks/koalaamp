// SPDX-License-Identifier: MIT
//
// PleditRenderer — the classic Winamp playlist editor, drawn from decoded skin
// sheets into a QImage. A pure function of (Skin, PleditColors, PleditState).
//
// The window is built from the `pledit.bmp` tiles (docs/spec.md §5.4): a 20 px
// title band, a middle band whose height is the visible track rows, and a 38 px
// bottom band. Horizontal growth adds 25 px tiles, vertical growth 29 px tiles
// (Webamp `constants.ts`: WINDOW_RESIZE_SEGMENT_WIDTH/HEIGHT). Structure,
// element rects and sprite choices are ported from Webamp (MIT)
// `css/playlist-window.css` + `js/skinSelectors.ts` @
// webamp@e6283c0eabe9b1313cd41df16d2875e897719696; attribution in LICENSES.md.
//
// Rendering rules (docs/spec.md §5.2): exact integer blits only, integer
// `scale` applied after the 1x composition (nearest neighbour).
//
// Track text uses the `pledit.txt` `Font=` family when that family is actually
// installed, and otherwise the skin's 5x6 `text.bmp` font — the classic
// fallback, which is what makes the committed goldens platform-stable.
#pragma once

#include <cstddef>
#include <string>

#include <QColor>
#include <QImage>
#include <QRect>

#include "skins/Skin.hpp"
#include "skins/SkinText.hpp"
#include "ui/PleditState.hpp"

namespace koalaamp::ui {

// --- geometry (1x skin pixels) ----------------------------------------------
QRect pleditTopBar(const PleditState& state);
QRect pleditTitleBar(const PleditState& state);
QRect pleditTopLeftCorner(const PleditState& state);
QRect pleditTopRightCorner(const PleditState& state);
QRect pleditCloseButton(const PleditState& state);
QRect pleditShadeButton(const PleditState& state);

QRect pleditMiddle(const PleditState& state);
QRect pleditMiddleLeft(const PleditState& state);
QRect pleditMiddleRight(const PleditState& state);
QRect pleditListArea(const PleditState& state);
QRect pleditTrackRow(const PleditState& state, int visibleRow);

QRect pleditScrollBar(const PleditState& state);
QRect pleditScrollHandle(const PleditState& state);
QRect pleditScrollUp(const PleditState& state);
QRect pleditScrollDown(const PleditState& state);

QRect pleditBottom(const PleditState& state);
QRect pleditBottomLeft(const PleditState& state);
QRect pleditBottomRight(const PleditState& state);
QRect pleditVisualizer(const PleditState& state);
QRect pleditRunningTime(const PleditState& state);
QRect pleditMiniTime(const PleditState& state);
QRect pleditResizeGrip(const PleditState& state);
/// Menu buttons in the bottom-left band: 0 add, 1 remove, 2 selection, 3 misc.
QRect pleditMenuButton(const PleditState& state, int menu);
/// Bottom transport buttons: 0 prev, 1 play, 2 pause, 3 stop, 4 next, 5 eject.
QRect pleditActionButton(const PleditState& state, int which);

// --- row visuals ------------------------------------------------------------

/// The colours a row paints with, decided by selection / current / hover.
struct PleditRowVisual {
    QColor background;
    QColor text;
    bool queued = false;
    bool operator==(const PleditRowVisual&) const = default;
};
PleditRowVisual pleditRowVisual(const skins::PleditColors& colors, const PleditState& state,
                                std::size_t index);

/// Row label: the (padded) track number, title, queue marker and duration.
std::string pleditRowLabel(const PleditState& state, std::size_t index);
/// `m:ss`, or "--:--" for an unknown length.
std::string formatPleditDuration(int lengthMs);

// --- text --------------------------------------------------------------------

/// True when `family` is installed and can be used for track rows; an empty or
/// absent family falls back to the 5x6 skin font.
bool pleditFontAvailable(const std::string& family);

// --- rendering ---------------------------------------------------------------

/// Composes the playlist window at 1x times `scale` (>= 1).
QImage renderPleditWindow(const skins::Skin& skin, const skins::PleditColors& colors,
                          const PleditState& state, int scale = 1);

}  // namespace koalaamp::ui
