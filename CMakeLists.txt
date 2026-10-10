# SPDX-License-Identifier: MIT
# KoalaAmp — clean-room classic-Winamp-style music player (Qt 6 / C++20).
# Root superbuild. Module layout and layering rules: docs/spec.md §2.1-2.3.

cmake_minimum_required(VERSION 3.24)

project(KoalaAmp
    VERSION 0.0.1
    DESCRIPTION "Clean-room classic-Winamp-style music player"
    HOMEPAGE_URL "https://github.com/idontwantyourspamthanks/koalaamp"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Language & build defaults ---------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# --- ccache (opportunistic; never required) --------------------------------
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "KoalaAmp: using ccache at ${CCACHE_PROGRAM}")
endif()

# --- Qt ---------------------------------------------------------------------
# The project targets Qt 6.12 LTS, but dev machines may legitimately carry an
# older 6.10.x (this repo's reference Linux box has 6.10.2). The Widgets/Gui/
# OpenGL APIs used through M0 are stable across that range, so accept >= 6.10;
# CI installs its own pinned 6.12. See docs/spikes/fe0-deps.md.
#
# LGPLv3 modules only (Widgets/Gui/OpenGL). QML and the copyleft-only Qt
# modules (Quick3D, Wayland Compositor, ...) must never enter the dependency
# set — scripts/license-audit.sh enforces the inventory.
find_package(Qt6 6.10 REQUIRED COMPONENTS Widgets Gui OpenGL)

if(Qt6_VERSION VERSION_LESS 6.12)
    message(STATUS
        "KoalaAmp: Qt ${Qt6_VERSION} found; project target is 6.12 LTS "
        "(local tools may lag — see docs/spikes/fe0-deps.md).")
else()
    message(STATUS "KoalaAmp: Qt ${Qt6_VERSION} (target line)")
endif()

include(CompilerWarnings)

# --- Targets ----------------------------------------------------------------
add_subdirectory(src)

include(CTest)                  # defines BUILD_TESTING (default ON)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

add_subdirectory(spikes)        # M0 verification probes (FE-1, ...)
