# SPDX-License-Identifier: MIT
add_library(koalaamp_interop INTERFACE)
target_include_directories(koalaamp_interop INTERFACE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(koalaamp_interop INTERFACE koalaamp_library)
