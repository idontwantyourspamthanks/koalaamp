# SPDX-License-Identifier: MIT
# Module tree (docs/spec.md §2.1). Each module is a target; interfaces live in
# headers first, implementations land in the owning tickets.
#
# Layering: ui is the only Qt-Widgets-dependent layer. playback/library/
# servers/skins talk to each other through the abstract interfaces declared in
# their headers, never through concrete types.

add_subdirectory(library)   # ITrackSource, Track
add_subdirectory(playback)  # IAudioEngine
add_subdirectory(servers)   # IServerSource (Jellyfin/Plex)
add_subdirectory(skins)     # ISkinLoader (WSZ)
add_subdirectory(interop)   # third-party façades (libvlc, projectM, TagLib, ...)
add_subdirectory(ui)        # Qt Widgets — windows

# The application binary: minimal frameless window until FE-1/FE-2 land.
add_executable(koalaamp main.cpp)
koalaamp_enable_warnings(koalaamp)
target_link_libraries(koalaamp PRIVATE koalaamp_ui)
