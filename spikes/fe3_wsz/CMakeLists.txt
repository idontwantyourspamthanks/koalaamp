# SPDX-License-Identifier: MIT
# FE-3 spike — load base-2.91.wsz and render the classic main window
# (docs/tickets.md · FE-3, deliverable docs/spikes/fe3-wsz-render.md).
#
# SE-1 promoted the decoder core (ZipReader/BmpDecoder/Skin/SkinSprites) into
# src/skins/; this spike now links that app module for the decode path and keeps
# only its own renderer (rendering is SE-2's job, not the app's yet).
find_package(Qt6 REQUIRED COMPONENTS Gui)

add_library(koalaamp_fe3_wsz STATIC
    MainWindowRenderer.cpp)
koalaamp_enable_warnings(koalaamp_fe3_wsz)
target_include_directories(koalaamp_fe3_wsz PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(koalaamp_fe3_wsz PUBLIC koalaamp_skins Qt6::Gui)

add_executable(spike_fe3 main.cpp)
koalaamp_enable_warnings(spike_fe3)
target_link_libraries(spike_fe3 PRIVATE koalaamp_fe3_wsz)
target_compile_definitions(spike_fe3 PRIVATE
    "FE3_DEFAULT_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\"")
