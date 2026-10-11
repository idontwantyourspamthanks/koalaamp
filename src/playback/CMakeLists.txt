# SPDX-License-Identifier: MIT
# playback — Qt-free playback service (docs/spec.md §2.1-2.3).
#
# `koalaamp_playback` stays a header-only INTERFACE target: consumers (ui,
# tests) can include playback/IAudioEngine.hpp and playback/IPlaybackObserver.hpp
# without pulling libvlc into their link line.
add_library(koalaamp_playback INTERFACE)
target_include_directories(koalaamp_playback INTERFACE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(koalaamp_playback INTERFACE koalaamp_library)

# The FE-4 implementation wraps the FE-2 libvlc callback chain (VlcPcmTap →
# SpscRing → MiniAudioSink), so it exists only where that chain does. interop/
# sets KOALAAMP_HAS_LIBVLC and src/CMakeLists.txt adds interop first for that
# reason.
if(KOALAAMP_HAS_LIBVLC)
    add_library(koalaamp_playback_core STATIC
        PlaybackCore.cpp
        PlaybackCore.hpp
        IPlaybackObserver.hpp
        IAudioEngine.hpp)
    koalaamp_enable_warnings(koalaamp_playback_core)
    target_include_directories(koalaamp_playback_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(koalaamp_playback_core PUBLIC koalaamp_playback koalaamp_vlc)
    message(STATUS "KoalaAmp: FE-4 PlaybackCore enabled (libvlc + miniaudio)")
else()
    message(STATUS "KoalaAmp: FE-4 PlaybackCore disabled (libvlc or vendored miniaudio missing)")
endif()
