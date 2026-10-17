// SPDX-License-Identifier: MIT
//
// GenRenderer — the classic skin's "general purpose" (`gen.bmp`) window frame:
// the tiled chrome Winamp gives plugin windows (AVS, Media Library), and the
// surface VZ-1's visualizer window is built on.
//
// Geometry comes from the sprite table (`skins/SkinSprites.hpp` `kSpritesGEN`,
// `gen.bmp` 194x109 per docs/spikes/fe3-wsz-render.md §5): a 20 px title bar of
// 25 px tiles (left corner, repeated fill, right corner), 11 px / 8 px side
// borders tiled in 29 px steps and closed by their `*_BOTTOM` variants, and a
// 14 px bottom bar of 25 px tiles. Only the corners are fixed — every middle
// piece repeats, which is how gen windows resize (docs/research/skins.md §1.4).
//
// `gen.bmp` is optional in a skin (`skins/SkinSet.cpp`) and many omit it, so the
// fallback below paints a neutral dark frame with the skin's 5x6 font for the
// title; the visualizer stays a usable, movable window either way.
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
/// The close box (`GEN_CLOSE_SELECTED` is 9x9), inset from the right border.
inline constexpr int kGenCloseSize = 9;
inline constexpr int kGenCloseMargin = 2;

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
/// reference size and a whole number of tiling steps larger.
QSize genSnapSize(const QSize& wanted);

/// The composing frame: title bar, borders, bottom bar, close box and the
/// client area. `active` picks the skin's selected title-bar sprites; `title` is
/// drawn in the skin's 5x6 font (uppercase-folded) or, without it, in the
/// fallback font. Returns a `genSnapSize(contentSize) * scale` image.
QImage renderGenWindow(const skins::Skin& skin, const QSize& contentSize, const std::string& title,
                       bool active, int scale = 1);

/// True when the fallback frame would be used (the skin ships no `gen.bmp`).
bool genUsesFallback(const skins::Skin& skin);

}  // namespace koalaamp::ui
