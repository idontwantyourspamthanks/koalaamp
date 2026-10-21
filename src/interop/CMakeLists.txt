# SPDX-License-Identifier: MIT
add_library(koalaamp_interop INTERFACE)
target_include_directories(koalaamp_interop INTERFACE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(koalaamp_interop INTERFACE koalaamp_library)

# --- libvlc (FE-2) ----------------------------------------------------------
# Raw C API headers (docs/spikes/fe0-deps.md: libvlcpp rejected — it would
# compile LGPL code into our binary). Discovered via pkg-config on Linux/BSD;
# vcpkg supplies a config package on Windows (find_package fallback) and
# Homebrew does on macOS. LGPL discipline: only the shared library is linked.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBVLC QUIET IMPORTED_TARGET libvlc)
endif()
if(NOT LIBVLC_FOUND)
    find_package(LibVLC QUIET) # vcpkg / Homebrew config package
endif()

# CI-1: the official Windows SDK zip has no CMake config. The CI job extracts it
# into third_party/vlc and exports KOALAAMP_VLC_SDK; create the same imported
# target shape the vcpkg/Homebrew path would provide.
if(WIN32 AND NOT LIBVLC_FOUND AND DEFINED ENV{KOALAAMP_VLC_SDK})
    find_path(LIBVLC_INCLUDE_DIR NAMES vlc/vlc.h
        HINTS "$ENV{KOALAAMP_VLC_SDK}/sdk/include")
    find_library(LIBVLC_LIBRARY NAMES libvlc
        HINTS "$ENV{KOALAAMP_VLC_SDK}/sdk/lib")
    if(LIBVLC_INCLUDE_DIR AND LIBVLC_LIBRARY)
        add_library(LibVLC::LibVLC UNKNOWN IMPORTED)
        set_target_properties(LibVLC::LibVLC PROPERTIES
            IMPORTED_LOCATION "${LIBVLC_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBVLC_INCLUDE_DIR}")
        set(LIBVLC_FOUND TRUE)
    endif()
endif()

if(LIBVLC_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/include/miniaudio.h")
    add_subdirectory(vlc)
    set(KOALAAMP_HAS_LIBVLC TRUE CACHE INTERNAL "libvlc + miniaudio available (FE-2 targets)")
    message(STATUS "KoalaAmp: libvlc found — FE-2 spike targets enabled")
elseif(LIBVLC_FOUND)
    message(STATUS
        "KoalaAmp: libvlc found but third_party/include/miniaudio.h missing — "
        "run scripts/vendor-deps.sh; FE-2 targets disabled")
else()
    message(STATUS "KoalaAmp: libvlc not found — FE-2 spike targets disabled "
        "(install libvlc-dev / run scripts/vendor-deps.sh)")
endif()

# CI-1's guard: the Windows runner must build the playback path. The SDK is
# fetched by .github/workflows/ci.yml; a missing target here is a regression in
# this coverage, not a tolerated local configuration.
if(WIN32 AND DEFINED ENV{KOALAAMP_REQUIRE_VLC_SDK} AND NOT KOALAAMP_HAS_LIBVLC)
    message(FATAL_ERROR
        "CI-1: KOALAAMP_REQUIRE_VLC_SDK is set but libvlc/miniaudio did not resolve")
endif()

# --- projectM (VZ-1) --------------------------------------------------------
# The visualizer's engine façade (IVisualizerEngine + the projectM backend). A stub
# when libprojectM is not vendored, so the build stays green on machines and CI jobs
# without it; consumers check KOALAAMP_HAS_PROJECTM / engineAvailable().
add_subdirectory(projectm)
