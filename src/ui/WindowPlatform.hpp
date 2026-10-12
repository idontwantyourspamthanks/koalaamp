// SPDX-License-Identifier: MIT
//
// ui — launch-time Qt platform selection (SE-4, spec §5.3 / FE-1 findings).
//
// Native Wayland has no client-side window positioning and no output-shaping
// protocol, so the window set cannot be snapped/docked or masked there. The
// documented posture (docs/spec.md §5.3, docs/feasibility.md) is to run under
// XWayland when the session is Wayland — the Audacious/Qmmp precedent. Qt
// selects its platform plugin from `QT_QPA_PLATFORM`, read **before**
// `QApplication` is constructed, so the request must happen first thing in
// `main()`.
//
// Fallback (documented): if XWayland is unavailable the launch fails; the user
// can set `QT_QPA_PLATFORM=wayland` explicitly to run compositor-placed with an
// alpha mask. An explicitly-set `QT_QPA_PLATFORM` is always respected.
#pragma once

namespace koalaamp::ui {

/// What `main()` should do about the platform plugin at launch.
enum class LaunchPlatform {
    KeepCurrent,      ///< X11/Windows/macOS/offscreen or an explicit choice
    RequestXWayland,  ///< wayland session, no explicit platform: force "xcb"
};

/// Pure decision, split out so it is unit-testable without a QApplication.
/// Each argument is the corresponding environment variable (nullptr/"" == unset).
LaunchPlatform launchPlatformPreference(const char* qtPlatform, const char* waylandDisplay,
                                        const char* sessionType) noexcept;

/// Applies `launchPlatformPreference()` to the process environment. MUST be
/// called before `QApplication`. Returns true when it forced "xcb".
bool requestXWaylandAtLaunch() noexcept;

}  // namespace koalaamp::ui
