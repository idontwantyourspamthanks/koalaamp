# SPDX-License-Identifier: MIT
# FE-14 spike — projectM inside a QOpenGLWidget (docs/tickets/FE-14-projectm-qt.md).
#
# libprojectM is acquired by scripts/vendor-deps.sh into third_party/projectm/
# as a SHARED library (LGPL-2.1-or-later, dynamically linked — never committed,
# never static). When it has not been vendored (e.g. the CI build matrix) the
# target is skipped so configure still succeeds on a bare checkout.

find_package(Qt6 6.10 REQUIRED COMPONENTS Widgets Gui OpenGL OpenGLWidgets)
find_package(OpenGL REQUIRED)

set(_pm_hints "${CMAKE_SOURCE_DIR}/third_party/projectm")
find_package(projectM4 CONFIG QUIET HINTS "${_pm_hints}")

if(NOT projectM4_FOUND)
    message(STATUS
        "FE-14 spike: libprojectM not found under third_party/projectm "
        "(run scripts/vendor-deps.sh) — skipping spike_fe14")
    return()
endif()

message(STATUS "FE-14 spike: libprojectM ${projectM4_VERSION} from ${projectM4_DIR}")

add_executable(spike_fe14
    main.cpp
    VizCanvas.cpp
    VizCanvas.hpp
    SyntheticPcm.hpp)

koalaamp_enable_warnings(spike_fe14)
target_include_directories(spike_fe14 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(spike_fe14 PRIVATE
    "FE14_DEFAULT_PRESET_DIR=\"${CMAKE_SOURCE_DIR}/third_party/presets/cream\"")

# projectM is linked dynamically; the vendored .so lives outside any system
# search path, so bake an RPATH pointing at the vendored prefix.
target_link_libraries(spike_fe14 PRIVATE
    Qt6::Widgets Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    OpenGL::GL libprojectM::projectM)
set_target_properties(spike_fe14 PROPERTIES
    BUILD_RPATH "${_pm_hints}/lib"
    INSTALL_RPATH "${_pm_hints}/lib")
