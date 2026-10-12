# SPDX-License-Identifier: MIT
# SE-5 spike — the Webamp pixel-diff harness for the classic equalizer window
# (docs/tickets/SE-5-eq-window.md). Renders `koalaamp::ui` EQ states to PNG so
# they can be diffed against Webamp's own render of the same skin
# (spikes/se5_eq/reference/capture.mjs + render-and-diff.sh).
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)

add_executable(spike_se5 main.cpp)
koalaamp_enable_warnings(spike_se5)
target_link_libraries(spike_se5 PRIVATE koalaamp_skins koalaamp_ui)
target_compile_definitions(spike_se5 PRIVATE
    "SE5_DEFAULT_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\"")
