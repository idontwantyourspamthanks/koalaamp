// SPDX-License-Identifier: MIT
//
// EqRenderer — the classic Winamp equalizer window, drawn from decoded skin
// sheets into a QImage. A pure function of (Skin, EqWindowState).
//
// Geometry and sprite selection are ported from Webamp (MIT)
// `css/equalizer-window.css`, `css/base-skin.css` (the generated sprite table)
// and `js/components/EqualizerWindow/*` at
// webamp@e6283c0eabe9b1313cd41df16d2875e897719696; the FE-3 spike's corrected
// sheet metrics apply (`eq_ex` is 275x82, not the published 275x54 —
// docs/research/skins.md §1.2, docs/spikes/fe3-wsz-render.md §5). Attribution in
// LICENSES.md.
//
// Rendering rules (docs/spec.md §5.2): exact integer blits only. `scale` is an
// integer upscale applied after the 1x composition.
//
// The slider groove is not a single sprite: `eqmain.bmp` packs 28 groove
// frames in a 14x2 grid inside the `EQ_SLIDER_BACKGROUND` rect (13,164,209,129),
// and Webamp indexes them with `sprite = round(value/100 * 27)`,
// `x = (sprite % 14) * 15`, `y = (sprite / 14) * 65`. The thumb is a single
// 11x11 sprite (`EQ_SLIDER_THUMB`), moved by `floor((62 - 11) * (1 - value/100))`.
// The window's own value is AU-1's inverted 0-63 field, so `value` below is the
// Webamp 0-100 equivalent (see `eqWebampValue`) — this is the only place the
// inversion is applied.
#pragma once

#include <QImage>
#include <QRect>

#include "skins/Skin.hpp"
#include "ui/EqState.hpp"

namespace koalaamp::ui {

/// Equalizer window constants (docs/spec.md §5.1).
inline constexpr int kEqWidth = 275;
inline constexpr int kEqHeight = 116;
inline constexpr int kEqShadeHeight = 14;

/// Destination rectangles of the equalizer window, in 1x skin pixels (Webamp
/// `css/equalizer-window.css` positions, verified against the live DOM in
/// `spikes/se5_eq/reference/`).
struct EqLayout {
    static constexpr QRect titleBar{0, 0, 275, 14};
    static constexpr QRect shadeButton{254, 3, 9, 9};
    static constexpr QRect closeButton{264, 3, 9, 9};
    static constexpr QRect onButton{14, 18, 26, 12};
    static constexpr QRect autoButton{40, 18, 32, 12};
    static constexpr QRect presetsButton{217, 18, 44, 12};
    static constexpr QRect graph{86, 17, 113, 19};
    static constexpr QRect preamp{21, 38, 14, 63};
    /// The 10 band sliders: `x = bandX0 + bandPitch * i`.
    static constexpr int bandX0 = 78;
    static constexpr int bandPitch = 18;
    static constexpr int bandY = 38;
    static constexpr int bandWidth = 14;
    static constexpr int bandHeight = 63;
    /// Clickable dB labels (Webamp `#plus12db`/`#zerodb`/`#minus12db`): they
    /// have no art of their own, only hit areas.
    static constexpr QRect plus12db{45, 36, 22, 8};
    static constexpr QRect zerodb{45, 64, 22, 8};
    static constexpr QRect minus12db{45, 95, 22, 8};
    /// Windowshade.
    static constexpr QRect shadeBackground{0, 0, 275, 14};
    static constexpr QRect shadeVolume{61, 4, 97, 6};
    static constexpr QRect shadeBalance{164, 4, 43, 6};
};

/// Slider geometry (Webamp `js/components/EqualizerWindow/Band.tsx` +
/// `VerticalSlider.tsx`).
struct EqSliderMetrics {
    static constexpr int thumbW = 11;
    static constexpr int thumbH = 11;
    static constexpr int trackH = 62;        ///< VerticalSlider `height`
    static constexpr int bodyH = 63;         ///< WinampButton `.band` height
    static constexpr int grooveCellW = 15;   ///< band-sheet cell pitch
    static constexpr int grooveCellH = 65;
    static constexpr int grooveCols = 14;    ///< 14 x 2 grid = 28 frames
    static constexpr int grooveFrames = 28;
    static constexpr int grooveSrcX = 13;    ///< EQ_SLIDER_BACKGROUND origin
    static constexpr int grooveSrcY = 164;
    static constexpr int shadeThumbW = 3;
    static constexpr int shadeThumbH = 7;
};

/// AU-1's inverted 0..63 field → Webamp's 0..100 slider value (100 = +12 dB at
/// the top). `a = 0` (full boost) → 100; `a = 63` (full cut) → 0.
int eqWebampValue(int auValue);

/// Inverse of `eqWebampValue`, clamped to AU-1's 0..63 field (used when a
/// pointer/keyboard gesture resolves to a Webamp 0..100 position).
int eqAuValueFromWebamp(int webampValue);

/// Groove frame index into the 14x2 grid, `round(value/100 * 27)`.
int eqGrooveFrame(int webampValue);
/// Source rectangle of a groove frame inside `eqmain.bmp`.
QRect eqGrooveSrcRect(int webampValue);
/// Thumb top offset inside the 63 px slider body, `floor((62-11)*(1-value/100))`.
int eqThumbTop(int webampValue);
/// Left offset of a mini shade slider thumb inside a track of `trackWidth`.
int eqShadeThumbLeft(int value, int minValue, int maxValue, int trackWidth);

/// x of band slider `index` (1..10); index 0 is the preamp.
int eqBandX(int sliderIndex);

/// The whole equalizer window ("EQMAIN" background + title bar + buttons +
/// graph + sliders), or the "EQ_EX" windowshade. `scale` must be >= 1.
QImage renderEqWindow(const skins::Skin& skin, const EqWindowState& state, int scale = 1);

}  // namespace koalaamp::ui
