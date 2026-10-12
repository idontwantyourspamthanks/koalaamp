# SPDX-License-Identifier: MIT
# Module tree (docs/spec.md §2.1). Each module is a target; interfaces live in
# headers first, implementations land in the owning tickets.
#
# Layering: ui is the only Qt-Widgets-dependent layer. playback/library/
# servers/skins talk to each other through the abstract interfaces declared in
# their headers, never through concrete types.

add_subdirectory(library)   # ITrackSource, Track
add_subdirectory(interop)   # third-party façades (libvlc, projectM, TagLib, ...)
                            # — before playback: it decides KOALAAMP_HAS_LIBVLC,
                            # which gates the FE-4 PlaybackCore target.
add_subdirectory(playback)  # IAudioEngine, PlaybackCore (FE-4)
add_subdirectory(audio)     # AU-1 DSP pipeline (ReplayGain, EQ, preamp, limiter)
add_subdirectory(playlist)  # PL-1 playlist formats + autosave (Qt-free)
add_subdirectory(servers)   # IServerSource (Jellyfin/Plex)
add_subdirectory(skins)     # ISkinLoader (WSZ)
add_subdirectory(ui)        # Qt Widgets — windows

# The application binary: the classic main window (SE-2), with the libvlc
# playback core attached when that target exists.
add_executable(koalaamp main.cpp)
koalaamp_enable_warnings(koalaamp)
target_link_libraries(koalaamp PRIVATE koalaamp_ui)
if(TARGET koalaamp_ui_playback)
    target_link_libraries(koalaamp PRIVATE koalaamp_ui_playback)
    target_compile_definitions(koalaamp PRIVATE KOALAAMP_HAS_PLAYBACK_CORE=1)
endif()
