// SPDX-License-Identifier: MIT
//
// SkinHitRegion — the clickable rects of the main window and the `region.txt`
// window shape.
//
// Hot areas follow Webamp's `css/main-window.css` box model exactly (including
// the windowshade geometry), so the pointer lands where Winamp's would
// (docs/spec.md §5.2). The mask comes from the skin's `region.txt`: polygons
// describe the *visible* area and become a QRegion (SetWindowRgn on Windows,
// XShape on X11). On platforms where a widget mask does not clip input (macOS
// CALayer) the same polygons answer `maskContains` for the app-level hit test.
#pragma once

#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <vector>

#include "skins/SkinText.hpp"
#include "ui/PlayerState.hpp"

namespace koalaamp::ui {

/// Every interactive element of the main window.
enum class HitId {
    None,
    TitleBar,
    OptionButton,
    MinimizeButton,
    ShadeButton,
    CloseButton,
    ClutterO,
    ClutterA,
    ClutterI,
    ClutterD,
    ClutterV,
    PlayPauseIndicator,
    WorkIndicator,
    Time,
    Marquee,
    Visualizer,
    Kbps,
    Khz,
    MonoStereo,
    Volume,
    Balance,
    EqButton,
    PlButton,
    Position,
    Previous,
    Play,
    Pause,
    Stop,
    Next,
    Eject,
    Shuffle,
    Repeat,
    ShadePosition,
    ShadeMiniTime,
};

struct HitRegion {
    HitId id;
    QRect rect;
};

/// Hot areas for the window in `state` (already scaled by `scale`).
std::vector<HitRegion> hitRegions(const PlayerState& state, int scale = 1);

/// Topmost hit for a window-local point (window pixels, not scaled).
HitId hitTest(const PlayerState& state, const QPoint& point, int scale = 1);

/// Builds the window mask for the normal or shaded state from `region.txt`.
/// Returns an empty QRegion when the skin defines no polygon for that state —
/// callers must then leave the widget unmasked (rectangular).
QRegion buildWindowMask(const skins::Region& region, bool shade, const QSize& size);

/// Point-in-polygon test over the same data, for platforms whose widget mask
/// does not clip input. Returns true when no polygon is defined (unmasked).
bool maskContains(const skins::Region& region, bool shade, const QPoint& point);

}  // namespace koalaamp::ui
