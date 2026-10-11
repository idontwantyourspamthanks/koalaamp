// SPDX-License-Identifier: MIT
//
// Composes the classic main window (275x116) from decoded skin sheets with
// exact integer (nearest-neighbour) blits, per docs/spec.md §5.2. Layout
// constants are ported from Webamp's `css/main-window.css` (MIT).
#pragma once

#include <optional>
#include <string>

#include <QImage>

#include "skins/Skin.hpp"

namespace fe3 {

enum class MediaStatus { Stopped, Playing, Paused };

/// The subset of player state the FE-3 spike renders. Defaults reproduce
/// Webamp's no-track startup: stopped, volume 78, balance centred, EQ and
/// playlist open, marquee "winamp 2.91".
struct MainWindowState {
    MediaStatus status = MediaStatus::Stopped;
    int volume = 78;   ///< 0..100, matching Webamp's default.
    int balance = 0;   ///< -100..100.
    bool shuffleSelected = false;
    bool repeatSelected = false;
    bool eqSelected = true;
    bool plSelected = true;
    int channels = 0;  ///< 0 = unknown, 1 = mono, 2 = stereo.
    std::string marquee = "winamp 2.91";
    int timeSeconds = -1;  ///< < 0 hides the time (as Webamp does while stopped).
    bool working = false;

    /// Transport button currently held down (sprite "_ACTIVE"): "", "previous",
    /// "play", "pause", "stop", "next", "eject".
    std::string activeButton;
};

/// Renders the window into a fresh 275x116 RGB32 image.
QImage renderMainWindow(const koalaamp::skins::Skin &skin, const MainWindowState &state);

}  // namespace fe3
