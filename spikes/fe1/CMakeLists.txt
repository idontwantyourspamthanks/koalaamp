# SPDX-License-Identifier: MIT
# FE-1 spike: masked frameless toplevel + QOpenGLWidget child.
#
# Deliberately standalone (does not link koalaamp_ui) so the main skeleton is
# untouched, per the ticket. Qt Widgets/Gui/OpenGL only.

# QOpenGLWidget lives in Qt6::OpenGLWidgets (LGPLv3, same set as Widgets/Gui/
# OpenGL). Declared locally so the M0 skeleton's Qt module set — and its
# license-audit manifest — is not touched by a throwaway spike.
find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets)

add_executable(spike_fe1 spike_fe1.cpp)
koalaamp_enable_warnings(spike_fe1)
target_link_libraries(spike_fe1 PRIVATE Qt6::Widgets Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets)
