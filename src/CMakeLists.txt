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
add_subdirectory(scrobble)  # SC-1: client-side Last.fm/ListenBrainz scrobbling (Qt-free)
add_subdirectory(skins)     # ISkinLoader (WSZ)
add_subdirectory(ui)        # Qt Widgets — windows

# The application binary: the classic main window (SE-2), with the libvlc
# playback core attached when that target exists.
add_executable(koalaamp main.cpp)
# SK-2: the Skins menu offers the bundled skins; the tests already compile this in.
target_compile_definitions(koalaamp PRIVATE
    "KOALAAMP_BUNDLED_SKINS=\"${CMAKE_SOURCE_DIR}/skins\""
    # VZ-1: the bundled starter presets, so a fresh install has something to show.
    "KOALAAMP_BUNDLED_PRESETS=\"${CMAKE_SOURCE_DIR}/presets\"")
koalaamp_enable_warnings(koalaamp)
# Last.fm desktop auth uses an application-wide API key/shared secret. These are
# registered by the maintainer, never entered by users. Leave empty for a build
# where Last.fm is simply unavailable (ListenBrainz still works).
set(KOALAAMP_LASTFM_API_KEY "" CACHE STRING "Last.fm application API key")
set(KOALAAMP_LASTFM_SHARED_SECRET "" CACHE STRING "Last.fm application shared secret")
if(KOALAAMP_LASTFM_API_KEY)
    target_compile_definitions(koalaamp PRIVATE
        "KOALAAMP_BUILTIN_LASTFM_API_KEY=\"${KOALAAMP_LASTFM_API_KEY}\"")
endif()
if(KOALAAMP_LASTFM_SHARED_SECRET)
    target_compile_definitions(koalaamp PRIVATE
        "KOALAAMP_BUILTIN_LASTFM_SHARED_SECRET=\"${KOALAAMP_LASTFM_SHARED_SECRET}\"")
endif()
target_link_libraries(koalaamp PRIVATE koalaamp_ui)
if(TARGET koalaamp_ui_playback)
    target_link_libraries(koalaamp PRIVATE koalaamp_ui_playback koalaamp_scrobble koalaamp_library)
    target_compile_definitions(koalaamp PRIVATE KOALAAMP_HAS_PLAYBACK_CORE=1)
endif()
# VZ-1: the app creates the visualizer's engine (the UI layer never links the back
# end), so the facade - and with it libprojectM, or its stub - is linked here.
if(KOALAAMP_HAS_GL_VISUALIZER AND TARGET koalaamp_interop_projectm)
    target_link_libraries(koalaamp PRIVATE koalaamp_interop_projectm)
    if(KOALAAMP_HAS_PROJECTM)
        set_target_properties(koalaamp PROPERTIES
            BUILD_RPATH "${KOALAAMP_PROJECTM_LIBDIR}")
    endif()
endif()
