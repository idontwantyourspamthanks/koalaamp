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

# --- projectM (VZ-1) --------------------------------------------------------
# The visualizer's engine façade (IVisualizerEngine + the projectM backend). A stub
# when libprojectM is not vendored, so the build stays green on machines and CI jobs
# without it; consumers check KOALAAMP_HAS_PROJECTM / engineAvailable().
add_subdirectory(projectm)
