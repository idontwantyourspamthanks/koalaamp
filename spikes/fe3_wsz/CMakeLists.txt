# SPDX-License-Identifier: MIT
# FE-3 spike — load base-2.91.wsz and render the classic main window
# (docs/tickets.md · FE-3, deliverable docs/spikes/fe3-wsz-render.md).
#
# Standalone: does not link any app module. The shared static library is used by
# both the spike binary and the Catch2 decoder/table tests.
find_package(Qt6 REQUIRED COMPONENTS Gui)
find_package(ZLIB REQUIRED)

add_library(koalaamp_fe3_wsz STATIC
    ZipReader.cpp
    BmpDecoder.cpp
    Skin.cpp
    MainWindowRenderer.cpp)
koalaamp_enable_warnings(koalaamp_fe3_wsz)
target_include_directories(koalaamp_fe3_wsz PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(koalaamp_fe3_wsz PUBLIC Qt6::Gui PRIVATE ZLIB::ZLIB)

add_executable(spike_fe3 main.cpp)
koalaamp_enable_warnings(spike_fe3)
target_link_libraries(spike_fe3 PRIVATE koalaamp_fe3_wsz)
target_compile_definitions(spike_fe3 PRIVATE
    "FE3_DEFAULT_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\"")
