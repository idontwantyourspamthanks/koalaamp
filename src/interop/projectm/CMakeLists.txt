# SPDX-License-Identifier: MIT
# projectM façade (VZ-1): the IVisualizerEngine seam and its projectM backend.
#
# libprojectM is fetched and built by scripts/vendor-deps.sh into third_party/projectm/
# as a SHARED library (LGPL-2.1-or-later — linked dynamically, never committed, never
# static; AGENTS.md). Like libvlc above, a machine without it is not an error: the
# façade then compiles a stub whose available() is false, so a bare checkout and the
# CI build matrix still configure, build and test. The RPATH mirrors the FE-14 spike
# (spikes/fe14_projectm/CMakeLists.txt): the vendored .so lives outside every system
# search path, so executables that link it must be told where to find it.

set(_pm_hints "${CMAKE_SOURCE_DIR}/third_party/projectm")
find_package(projectM4 CONFIG QUIET HINTS "${_pm_hints}")

add_library(koalaamp_interop_projectm STATIC
    ProjectMEngine.cpp
    VisualizerEngine.hpp)
koalaamp_enable_warnings(koalaamp_interop_projectm)
target_include_directories(koalaamp_interop_projectm PUBLIC "${CMAKE_SOURCE_DIR}/src")

if(projectM4_FOUND)
    target_include_directories(koalaamp_interop_projectm PRIVATE "${_pm_hints}/include")
    target_link_libraries(koalaamp_interop_projectm PUBLIC libprojectM::projectM)
    target_compile_definitions(koalaamp_interop_projectm PUBLIC KOALAAMP_HAS_PROJECTM=1)
    # Consumers (the app, the tests) add this to their RPATH when they link the
    # façade, since the .so is not installed anywhere the loader searches.
    set(KOALAAMP_PROJECTM_LIBDIR "${_pm_hints}/lib" CACHE INTERNAL
        "vendored libprojectM directory (consumers put it on their RPATH)")
    set(KOALAAMP_HAS_PROJECTM TRUE CACHE INTERNAL "libprojectM available (VZ-1)")
    message(STATUS "KoalaAmp: libprojectM ${projectM4_VERSION} — visualizer engine enabled")
else()
    set(KOALAAMP_HAS_PROJECTM FALSE CACHE INTERNAL "libprojectM available (VZ-1)")
    message(STATUS
        "KoalaAmp: libprojectM not found — the visualizer builds as a no-op stub "
        "(run scripts/vendor-deps.sh)")
endif()
