// SPDX-License-Identifier: MIT
//
// EqWindowState — the complete renderable state of the classic Winamp
// equalizer window (docs/tickets/SE-5-eq-window.md).
//
// Like `PlayerState`, the EQ renderer is a pure function of (Skin,
// EqWindowState): no widget, no timer, no engine. Every pixel the window can
// paint — including which slider is being dragged — is a field here, so a state
// can be rendered offscreen for the golden-screenshot harness.
//
// Slider values are AU-1's Winamp 6-bit field (src/audio/EqValue.hpp): 0..63
// where 0 = +12 dB, 31 = 0 dB and 63 = -12 dB. The window *stores and routes*
// these values verbatim; it never recomputes gains. Because the scale is
// inverted against intuition, the renderer is the only place that flips it (to
// put +12 dB at the top of the groove); no other code may.
#pragma once

#include <array>

namespace koalaamp::ui {

/// Number of EQ bands (Winamp table; src/audio/EqValue.hpp `kEqBands`).
inline constexpr int kEqWindowBands = 10;
/// Sliders = preamp + 10 bands.
inline constexpr int kEqWindowSliders = kEqWindowBands + 1;
/// Unity slider value (0 dB).
inline constexpr int kEqValueUnity = 31;
/// Full boost (+12 dB) and full cut (-12 dB) ends of the inverted field.
inline constexpr int kEqValueBoost = 0;
inline constexpr int kEqValueCut = 63;

/// Which slider a hit/state index refers to (index 0 is the preamp).
enum class EqSlider {
    Preamp = 0,
    Hz60 = 1,
    Hz170 = 2,
    Hz310 = 3,
    Hz600 = 4,
    Hz1000 = 5,
    Hz3000 = 6,
    Hz6000 = 7,
    Hz12000 = 8,
    Hz14000 = 9,
    Hz16000 = 10,
};

/// Everything the equalizer window paints.
struct EqWindowState {
    // --- window chrome ------------------------------------------------------
    bool shade = false;    ///< windowshade (275x14)
    bool selected = true;  ///< active window: titlebar "selected" art
    bool closePressed = false;
    bool shadePressed = false;

    // --- buttons ------------------------------------------------------------
    bool onPressed = false;      ///< ON held down
    bool autoPressed = false;    ///< AUTO held down
    bool presetsPressed = false; ///< PRESETS held down

    // --- EQ model -----------------------------------------------------------
    /// Slider values, index 0 = preamp then the 10 bands. AU-1's 0..63 field.
    std::array<int, kEqWindowSliders> sliders{
        kEqValueUnity, kEqValueUnity, kEqValueUnity, kEqValueUnity, kEqValueUnity,
        kEqValueUnity, kEqValueUnity, kEqValueUnity, kEqValueUnity, kEqValueUnity,
        kEqValueUnity};
    bool enabled = false;  ///< ON/OFF (bypass)
    bool autoLoad = false; ///< AUTO (per-album auto-load via AU-1's AlbumEqStore)
    /// Index into the current preset set, or -1 while the sliders are custom.
    int presetIndex = -1;

    /// Slider currently held/dragged (index into `sliders`), or -1.
    int activeSlider = -1;

    // --- shade previews -----------------------------------------------------
    int volume = 78;  ///< 0..100, mirrored from the main window
    int balance = 0;  ///< -100..100

    bool operator==(const EqWindowState&) const = default;
};

/// True when every slider is at unity (0 dB) — the flat preset.
inline bool isFlat(const EqWindowState& state) {
    for (const int value : state.sliders) {
        if (value != kEqValueUnity) {
            return false;
        }
    }
    return true;
}

}  // namespace koalaamp::ui
