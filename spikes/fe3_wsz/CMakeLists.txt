# SPDX-License-Identifier: MIT
# FE-3 spike — the Webamp pixel-diff harness for the classic main window
# (docs/tickets.md · FE-3, deliverable docs/spikes/fe3-wsz-render.md).
#
# SE-1 promoted the decoder core (ZipReader/BmpDecoder/Skin/SkinSprites) into
# src/skins/. SE-2 superseded this spike's own renderer
# (`MainWindowRenderer.cpp`, deleted) with the app renderer `src/ui`, so the
# harness now diffs exactly the code the product paints with — same command,
# same numbers (see the SE-2 section of the ticket).
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)

add_executable(spike_fe3 main.cpp)
koalaamp_enable_warnings(spike_fe3)
target_link_libraries(spike_fe3 PRIVATE koalaamp_skins koalaamp_ui)
target_compile_definitions(spike_fe3 PRIVATE
    "FE3_DEFAULT_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\"")
