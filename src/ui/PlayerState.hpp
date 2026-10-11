// SPDX-License-Identifier: MIT
//
// PlayerState — the complete renderable state of the classic Winamp main
// window (docs/spec.md §5.1-5.2, docs/tickets/SE-2-main-window.md).
//
// The renderer is a pure function of (Skin, VisColor, PlayerState): no widget,
// no timer, no engine. Everything the window can look like — including the
// frame of animation the visualiser is on and where the title marquee has
// scrolled to — is a field here, so a state can be rendered offscreen for the
// golden-screenshot harness (tests/se2_render_test.cpp) and reproduced exactly.
//
// Layout/behaviour constants ported from Webamp (MIT, `css/main-window.css` and
// `js/visualizer/*` @ webamp@e6283c0eabe9b1313cd41df16d2875e897719696) —
// attribution in LICENSES.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace koalaamp::ui {

/// Transport state, mirroring playback::PlaybackState (kept Qt-free here so the
/// renderer and its tests do not depend on the playback module).
enum class PlayerStatus { Stopped, Paused, Playing };

/// Visualiser mode the vis area cycles through on click. Classic Winamp 2.x
/// (and Webamp, `Pb = ["BAR", "OSCILLOSCOPE", "NONE"]`) has exactly three
/// states; the four-state "both" cycle is a Winamp 5 addition and out of v1
/// scope (docs/spec.md §1.1 "classic Winamp 2.95-era experience").
enum class VisMode { Spectrum, Oscilloscope, Off };

/// Number of spectrum bars / oscilloscope points in the main window
/// (docs/research/formats.md §5: Winamp draws 75, 576 is the FFT capacity).
inline constexpr int kVisBars = 75;

/// Sentinel for "no peak marker" (Webamp sets barPeak to -3 for <1 px peaks).
inline constexpr std::uint8_t kPeakOff = 0xFF;

/// One analysis frame of the in-window visualiser.
struct VisFrame {
    /// Spectrum bar heights, 0..15 (the vis area is 16 px tall).
    std::array<std::uint8_t, kVisBars> bars{};
    /// Peak-marker height per bar, 0..15, or kPeakOff for "do not draw".
    std::array<std::uint8_t, kVisBars> peaks{};
    /// Oscilloscope sample positions, in canvas rows 0..15.
    std::array<std::uint8_t, kVisBars> osc{};
    bool barsValid = false;
    bool oscValid = false;

    bool operator==(const VisFrame&) const = default;

    void clear() { *this = VisFrame{}; }
};

/// Clutterbar buttons (the `O`/`A`/`I`/`D`/`V` letters next to the title bar).
struct ClutterBar {
    bool enabled = true;  ///< `#clutter-bar.disabled` uses the dimmed sheet.
    bool o = false;       ///< Options       (Ctrl+O)
    bool a = false;       ///< Always on top
    bool i = false;       ///< File info
    bool d = false;       ///< Double size
    bool v = false;       ///< Visualiser (milkdrop) toggled
    bool operator==(const ClutterBar&) const = default;
};

/// Titlebar window buttons currently held down (sprite `_DEPRESSED`).
struct WindowButtons {
    bool option = false;
    bool minimize = false;
    bool shade = false;
    bool close = false;
    bool operator==(const WindowButtons&) const = default;
};

/// Transport/drag buttons currently held down (sprite `_ACTIVE`), matching
/// Webamp's `.winamp-active` class.
struct ActiveButtons {
    bool previous = false;
    bool play = false;
    bool pause = false;
    bool stop = false;
    bool next = false;
    bool eject = false;
    bool operator==(const ActiveButtons&) const = default;
};

/// Everything the main window paints.
struct PlayerState {
    // --- window chrome ------------------------------------------------------
    PlayerStatus status = PlayerStatus::Stopped;
    bool shade = false;      ///< windowshade (275x14)
    bool selected = true;    ///< active window: titlebar/shade "selected" art
    bool llama = false;      ///< easter-egg title bar (Ctrl+D llama)
    ClutterBar clutter;
    WindowButtons pressed;
    ActiveButtons active;
    bool eqSelected = true;      ///< EQ toggle button drawn selected
    bool plSelected = true;      ///< Playlist toggle button drawn selected
    bool eqDepressed = false;    ///< toggle held down
    bool plDepressed = false;

    // --- playback status ----------------------------------------------------
    bool working = false;        ///< `#work-indicator` (buffering/cueing)
    /// Time display. `timeVisible == false` hides the whole time field (Winamp
    /// hides it while stopped — Webamp `.stop #time { display: none }`).
    bool timeVisible = false;
    int timeSeconds = 0;
    bool countdown = false;      ///< remaining instead of elapsed
    bool timeBlinkOn = true;     ///< pause blink phase (2s on / 2s off)

    // --- media info ---------------------------------------------------------
    /// Bitrate in kbit/s; `kbpsKnown == false` renders Winamp's "  0" while a
    /// track is loaded without tags (Webamp `e.kbps || "0".padStart(3," ")`).
    int kbps = 0;
    bool kbpsKnown = false;
    int khz = 0;
    bool khzKnown = false;
    int channels = 0;            ///< 2 = stereo, 1 = mono, 0 = unknown

    // --- sliders ------------------------------------------------------------
    int volume = 78;             ///< 0..100 (Winamp default 78)
    int balance = 0;             ///< -100..100
    int position = 0;            ///< 0..100 percent complete
    bool volumeActive = false;   ///< drag in progress: `_SELECTED` thumb sprite
    bool balanceActive = false;
    bool positionActive = false;

    // --- toggles ------------------------------------------------------------
    bool shuffle = false;
    bool repeat = false;
    bool shuffleDepressed = false;  ///< held down: sprite `_DEPRESSED`
    bool repeatDepressed = false;

    // --- title marquee ------------------------------------------------------
    /// Composed display title (already "artist - title" formatted); the
    /// renderer uppercases/maps it through the 5x6 `text.bmp` font.
    std::string title;
    /// Scroll offset in whole pixels (see TitleText.hpp).
    int marqueeOffsetPx = 0;

    // --- visualiser ---------------------------------------------------------
    VisMode vis = VisMode::Spectrum;
    VisFrame visFrame;
};

/// FNV-1a hash of every field that affects the cached "chrome" — that is, all
/// of the state except the live `visFrame` (which the paint path redraws on
/// every animation tick, over the cached chrome). `SkinCanvas` uses this to
/// decide when a repaint needs a full re-composition.
inline std::uint64_t chromeKey(const PlayerState& state) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    const auto mixText = [&mix](std::string_view text) {
        for (const char c : text) {
            mix(static_cast<unsigned char>(c));
        }
    };
    mix(static_cast<std::uint64_t>(state.status));
    mix(state.shade);
    mix(state.selected);
    mix(state.llama);
    mix(static_cast<std::uint64_t>(state.clutter.enabled));
    mix(static_cast<std::uint64_t>(state.clutter.o));
    mix(static_cast<std::uint64_t>(state.clutter.a));
    mix(static_cast<std::uint64_t>(state.clutter.i));
    mix(static_cast<std::uint64_t>(state.clutter.d));
    mix(static_cast<std::uint64_t>(state.clutter.v));
    mix(static_cast<std::uint64_t>(state.pressed.option));
    mix(static_cast<std::uint64_t>(state.pressed.minimize));
    mix(static_cast<std::uint64_t>(state.pressed.shade));
    mix(static_cast<std::uint64_t>(state.pressed.close));
    mix(static_cast<std::uint64_t>(state.active.previous));
    mix(static_cast<std::uint64_t>(state.active.play));
    mix(static_cast<std::uint64_t>(state.active.pause));
    mix(static_cast<std::uint64_t>(state.active.stop));
    mix(static_cast<std::uint64_t>(state.active.next));
    mix(static_cast<std::uint64_t>(state.active.eject));
    mix(static_cast<std::uint64_t>(state.eqSelected));
    mix(static_cast<std::uint64_t>(state.plSelected));
    mix(static_cast<std::uint64_t>(state.eqDepressed));
    mix(static_cast<std::uint64_t>(state.plDepressed));
    mix(static_cast<std::uint64_t>(state.working));
    mix(static_cast<std::uint64_t>(state.timeVisible));
    mix(static_cast<std::uint64_t>(state.timeSeconds));
    mix(static_cast<std::uint64_t>(state.countdown));
    mix(static_cast<std::uint64_t>(state.timeBlinkOn));
    mix(static_cast<std::uint64_t>(state.kbps));
    mix(static_cast<std::uint64_t>(state.kbpsKnown));
    mix(static_cast<std::uint64_t>(state.khz));
    mix(static_cast<std::uint64_t>(state.khzKnown));
    mix(static_cast<std::uint64_t>(state.channels));
    mix(static_cast<std::uint64_t>(state.volume));
    mix(static_cast<std::uint64_t>(state.balance));
    mix(static_cast<std::uint64_t>(state.position));
    mix(static_cast<std::uint64_t>(state.volumeActive));
    mix(static_cast<std::uint64_t>(state.balanceActive));
    mix(static_cast<std::uint64_t>(state.positionActive));
    mix(static_cast<std::uint64_t>(state.shuffle));
    mix(static_cast<std::uint64_t>(state.repeat));
    mix(static_cast<std::uint64_t>(state.shuffleDepressed));
    mix(static_cast<std::uint64_t>(state.repeatDepressed));
    mixText(state.title);
    mix(static_cast<std::uint64_t>(state.marqueeOffsetPx));
    mix(static_cast<std::uint64_t>(state.vis));
    return hash;
}

}  // namespace koalaamp::ui
