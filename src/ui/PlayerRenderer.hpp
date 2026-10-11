// SPDX-License-Identifier: MIT
//
// PlayerRenderer — the classic Winamp main window, drawn from decoded skin
// sheets into a QImage. A pure function of (Skin, VisColor, PlayerState).
//
// Every destination rectangle and every sprite choice is ported from Webamp
// (MIT) `css/main-window.css` + `js/skinSelectors.ts` + `js/visualizer/*` at
// webamp@e6283c0eabe9b1313cd41df16d2875e897719696; the FE-3 spike proved the
// core of this layout byte-identical to Webamp for the no-track startup state
// and a toggled state (docs/spikes/fe3-wsz-render.md), and SE-2 extends the
// same table to every control. Attribution in LICENSES.md.
//
// Rendering rules (docs/spec.md §5.2): exact integer blits only — no scaling,
// no smoothing, no antialiasing on sprite edges. `scale` is an *integer*
// upscale applied after the 1x composition, so a 2x window is exactly the 1x
// window with every pixel doubled.
#pragma once

#include <QImage>
#include <QRect>

#include "skins/Skin.hpp"
#include "skins/SkinText.hpp"
#include "ui/PlayerState.hpp"

namespace koalaamp::ui {

/// Main-window constants (docs/spec.md §5.1: the anchor is `main.bmp` 275x116).
inline constexpr int kMainWidth = 275;
inline constexpr int kMainHeight = 116;
inline constexpr int kShadeHeight = 14;
inline constexpr int kVisWidth = 76;
inline constexpr int kVisHeight = 16;

/// Destination rectangles of the main window, in 1x skin pixels. The core of
/// this table is the FE-3 spike's (`spikes/fe3_wsz/`, whose renderer SE-2
/// superseded with this file), verified pixel-exact against Webamp by
/// `spikes/fe3_wsz/render-and-diff.sh`; SE-2 adds the controls FE-3 did not
/// need (media info, visualiser, windowshade, clutter bar, button states).
struct MainLayout {
    static constexpr QRect titleBar{0, 0, 275, 14};
    static constexpr QRect option{6, 3, 9, 9};
    static constexpr QRect minimize{244, 3, 9, 9};
    static constexpr QRect shade{254, 3, 9, 9};
    static constexpr QRect close{264, 3, 9, 9};
    static constexpr QRect clutterBar{10, 22, 8, 43};
    static constexpr QRect clutterO{0, 3, 8, 8};     ///< relative to clutterBar
    static constexpr QRect clutterA{0, 11, 8, 7};    ///< (Webamp: height 7)
    static constexpr QRect clutterI{0, 18, 8, 7};
    static constexpr QRect clutterD{0, 25, 8, 8};
    static constexpr QRect clutterV{0, 33, 8, 7};
    static constexpr QRect playPause{26, 28, 9, 9};
    static constexpr QRect workIndicator{24, 28, 3, 9};
    static constexpr QRect visualizer{24, 43, 76, 16};
    static constexpr QRect time{39, 26, 59, 13};
    static constexpr QRect marquee{111, 24, 154, 6};
    static constexpr QRect kbps{111, 43, 15, 6};
    static constexpr QRect khz{156, 43, 10, 6};
    static constexpr QRect mono{212, 41, 27, 12};
    static constexpr QRect stereo{239, 41, 29, 12};
    static constexpr QRect volume{107, 57, 68, 13};
    static constexpr QRect balance{177, 57, 38, 13};
    static constexpr QRect eqButton{219, 58, 23, 12};
    static constexpr QRect plButton{242, 58, 23, 12};
    static constexpr QRect position{16, 72, 248, 10};
    static constexpr QRect previous{16, 88, 23, 18};
    static constexpr QRect play{39, 88, 23, 18};
    static constexpr QRect pause{62, 88, 23, 18};
    static constexpr QRect stop{85, 88, 23, 18};
    static constexpr QRect next{108, 88, 22, 18};  ///< Webamp clips the 23px sprite
    static constexpr QRect eject{136, 89, 22, 16};
    static constexpr QRect shuffle{164, 89, 47, 15};
    static constexpr QRect repeat{210, 89, 28, 15};
};

/// Windowshade layout (275x14). The shade bar art already carries the transport
/// button glyphs; only the shade button, the mini time and the position bar are
/// composited on top (verified pixel-exact against Webamp's shade capture).
struct ShadeLayout {
    static constexpr QRect background{0, 0, 275, 14};
    static constexpr QRect shadeButton{254, 3, 9, 9};
    static constexpr QRect position{226, 4, 17, 7};
    static constexpr QRect positionThumb{0, 0, 3, 7};  ///< relative to position
    static constexpr QRect miniTime{127, 4, 25, 6};
    /// Character slots of the mini time, relative to `miniTime` (Webamp's
    /// `OC`/`LC` components: background cells then sign/minutes/seconds).
    static constexpr int miniTimeSlots[5] = {1, 7, 12, 20, 25};
    /// Time digits (`#time`), relative to the window: 4 slots, 9x13 each.
    static constexpr int digitX[4] = {48, 60, 78, 90};
    static constexpr int digitY = 26;
    static constexpr int digitW = 9;
    static constexpr int digitH = 13;
    /// The minus sign (only drawn in countdown mode).
    static constexpr QRect minusSign{38, 32, 5, 1};
    /// With `nums_ex.bmp` the minus sign becomes a full 9x13 digit-slot sprite.
    static constexpr QRect minusSignEx{39, 26, 9, 13};
};

/// Slider geometry (Webamp: thumb travels `trackWidth - thumbWidth`).
struct SliderMetrics {
    static constexpr int thumbW = 14;
    static constexpr int thumbH = 11;
    static constexpr int volumeTrackW = 65;   ///< `#volume input` width
    static constexpr int balanceTrackW = 38;  ///< `#balance` width
    static constexpr int balanceSheetX = 9;   ///< balance.bmp groove off-by-9 trap
    static constexpr int positionThumbW = 29;
    static constexpr int shadeThumbW = 3;
};

/// Volume frame: sprite = round(volume/100 * 28), offset = (sprite-1) * 15.
int volumeFrameOffset(int volume);
/// Balance frame: sprite = floor(|balance|/100 * 27), offset = sprite * 15.
int balanceFrameOffset(int balance);
/// Thumb left offset inside a track of `trackWidth` px for `thumbWidth` px.
int thumbLeft(int value, int minValue, int maxValue, int trackWidth, int thumbWidth);

/// Webamp's `PE` — the 3-character `kbps` field ("128", " 64", "1.4M"-ish).
std::string formatKbps(int kbps);
/// Webamp's `QE` — the 2-character `khz` field.
std::string formatKhz(int khz);

/// Composes a 275x(windowHeight*scale) image; `scale` must be >= 1.
QImage renderMainWindow(const skins::Skin& skin, const skins::VisColor& visColor,
                        const PlayerState& state, int scale = 1);

/// Renders a single player state to an ARGB premultiplied pixmap-free image
/// using the skin's own palette. Equivalent to `renderMainWindow`; provided so
/// the paint path has one entry point (SkinCanvas uses this).
QImage renderPlayerState(const skins::Skin& skin, const skins::VisColor& visColor,
                         const PlayerState& state, int scale);

/// The cacheable part of a frame: the whole window with the live visualiser
/// *columns* omitted. `SkinCanvas` blits this and then calls
/// `drawVisualizerOverlay` for the part that changes every animation tick.
QImage renderChrome(const skins::Skin& skin, const skins::VisColor& visColor,
                    const PlayerState& state, int scale);

/// Draws only the visualiser columns (spectrum bars + peaks, or the
/// oscilloscope) at `scale`.
void drawVisualizerOverlay(QPainter& painter, const skins::VisColor& visColor,
                           const PlayerState& state, int scale);

/// Window mask helpers live in SkinHitRegion.hpp (region.txt polygons).

}  // namespace koalaamp::ui
