// SPDX-License-Identifier: MIT
#include "ui/WindowPlatform.hpp"

#include <cstdlib>
#include <cstring>

#include <QByteArray>

#include <QtGlobal>

namespace koalaamp::ui {

LaunchPlatform launchPlatformPreference(const char* qtPlatform, const char* waylandDisplay,
                                        const char* sessionType) noexcept {
    // An explicit platform choice (including "offscreen" under ctest) wins.
    if (qtPlatform != nullptr && *qtPlatform != '\0') {
        return LaunchPlatform::KeepCurrent;
    }
    const bool onWayland = (waylandDisplay != nullptr && *waylandDisplay != '\0') ||
                           (sessionType != nullptr && std::strcmp(sessionType, "wayland") == 0);
    return onWayland ? LaunchPlatform::RequestXWayland : LaunchPlatform::KeepCurrent;
}

bool requestXWaylandAtLaunch() noexcept {
    const LaunchPlatform want = launchPlatformPreference(
        std::getenv("QT_QPA_PLATFORM"), std::getenv("WAYLAND_DISPLAY"),
        std::getenv("XDG_SESSION_TYPE"));
    if (want != LaunchPlatform::RequestXWayland) {
        return false;
    }
    qputenv("QT_QPA_PLATFORM", "xcb");
    return true;
}

}  // namespace koalaamp::ui
