// SPDX-License-Identifier: MIT
//
// PleditHitRegion — the clickable rects of the playlist window (the SE-2
// `SkinHitRegion` pattern applied to the playlist). The playlist is always
// rectangular (region.txt only shapes the main and EQ windows), so there is no
// mask here: every hit test is a plain rect containment on the 1x layout.
#pragma once

#include <QPoint>
#include <QRect>
#include <vector>

#include "ui/PleditState.hpp"

namespace koalaamp::ui {

/// Interactive elements of the playlist window (rows are separate: they carry
/// an index, see `pleditRowAt`).
enum class PleditHitId {
    None,
    TitleBar,
    CloseButton,
    ShadeButton,
    ResizeGrip,
    ScrollBar,
    ScrollUp,
    ScrollDown,
    Visualizer,
    RunningTime,
    MenuAdd,
    MenuRemove,
    MenuSelection,
    MenuMisc,
    ActionPrevious,
    ActionPlay,
    ActionPause,
    ActionStop,
    ActionNext,
    ActionEject,
};

struct PleditHitRegion {
    PleditHitId id;
    QRect rect;
};

/// Hot areas in paint order (later wins), already scaled.
std::vector<PleditHitRegion> pleditHitRegions(const PleditState& state, int scale = 1);

/// Topmost interactive element at a window-local point, in device pixels.
PleditHitId pleditHitTest(const PleditState& state, const QPoint& point, int scale = 1);

/// Visible list row at `point` (an absolute entry index, or -1). In shade mode
/// always -1.
int pleditRowAt(const PleditState& state, const QPoint& point, int scale = 1);

/// Menu index 0..3 for a menu-button hit, or -1.
int pleditMenuIndex(PleditHitId id);

}  // namespace koalaamp::ui
