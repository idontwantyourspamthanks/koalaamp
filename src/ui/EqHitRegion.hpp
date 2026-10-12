// SPDX-License-Identifier: MIT
//
// EqHitRegion — the clickable rects of the equalizer window (normal and
// windowshade), following Webamp's `css/equalizer-window.css` box model so the
// pointer lands where Winamp's would (docs/spec.md §5.2).
#pragma once

#include <QPoint>
#include <QRect>
#include <vector>

#include "ui/EqState.hpp"

namespace koalaamp::ui {

/// Every interactive element of the equalizer window.
enum class EqHitId {
    None,
    TitleBar,
    ShadeButton,
    CloseButton,
    OnButton,
    AutoButton,
    PresetsButton,
    /// The three dB labels: set every band to max (+12), unity, or min (-12).
    SetMaxDb,
    SetUnityDb,
    SetMinDb,
    /// A slider; `sliderIndex` identifies it (0 = preamp, 1..10 = bands).
    Slider,
    ShadeVolume,
    ShadeBalance,
};

struct EqHitRegion {
    EqHitId id;
    QRect rect;
    int sliderIndex = -1;  ///< only for EqHitId::Slider
};

/// Hot areas for the window in `state` (already scaled by `scale`).
std::vector<EqHitRegion> eqHitRegions(const EqWindowState& state, int scale = 1);

/// Topmost hit for a window-local point (window pixels, not scaled). Writes the
/// slider index through `sliderIndex` when non-null and the hit is a slider.
EqHitId eqHitTest(const EqWindowState& state, const QPoint& point, int scale = 1,
                  int* sliderIndex = nullptr);

}  // namespace koalaamp::ui
