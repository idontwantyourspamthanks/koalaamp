# SPDX-License-Identifier: MIT
# platform — cross-window process integration (CO-1). Qt Core/Network only.
add_library(koalaamp_platform STATIC
    SingleInstance.cpp
    SingleInstance.hpp)
koalaamp_enable_warnings(koalaamp_platform)
target_include_directories(koalaamp_platform PUBLIC "${CMAKE_SOURCE_DIR}/src")
find_package(Qt6 REQUIRED COMPONENTS Network)
target_link_libraries(koalaamp_platform PUBLIC Qt6::Core Qt6::Network)
