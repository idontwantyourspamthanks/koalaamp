# SPDX-License-Identifier: MIT
# ui — the only Qt-Widgets-dependent layer (docs/spec.md §2.2).

add_library(koalaamp_ui STATIC
    MainWindow.cpp
    MainWindow.hpp)

koalaamp_enable_warnings(koalaamp_ui)

# Public headers are addressed as "ui/<Header>.hpp" from the app and tests.
target_include_directories(koalaamp_ui
    PUBLIC "${CMAKE_SOURCE_DIR}/src")

# Widgets/Gui are PUBLIC: MainWindow.hpp exposes QWidget, so consumers (the app
# and widget tests) need the Qt include paths transitively.
target_link_libraries(koalaamp_ui
    PUBLIC koalaamp_library Qt6::Widgets Qt6::Gui
    PRIVATE Qt6::OpenGL)
