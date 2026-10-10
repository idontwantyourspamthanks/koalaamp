# SPDX-License-Identifier: MIT
add_library(koalaamp_servers INTERFACE)
target_include_directories(koalaamp_servers INTERFACE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(koalaamp_servers INTERFACE koalaamp_library)
