# SPDX-License-Identifier: MIT
add_library(koalaamp_skins INTERFACE)
target_include_directories(koalaamp_skins INTERFACE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(koalaamp_skins INTERFACE koalaamp_library)
