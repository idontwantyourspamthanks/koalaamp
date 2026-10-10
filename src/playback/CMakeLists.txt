# SPDX-License-Identifier: MIT
add_library(koalaamp_playback INTERFACE)
target_include_directories(koalaamp_playback INTERFACE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(koalaamp_playback INTERFACE koalaamp_library)
