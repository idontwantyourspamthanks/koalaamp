# SPDX-License-Identifier: MIT
# plex — plex.tv auth + PMS browse/source (SV-3). Qt-free.
#
# The generic HTTP seam and fixtures live in servers/jellyfin; the Plex module
# reuses them (transport-only) until it moves to a neutral servers/net target.
add_library(koalaamp_plex STATIC
    PlexAuth.cpp
    PlexGdm.cpp
    PlexClient.cpp
    PlexStream.cpp
    PlexSource.cpp
    PlexMediaSource.cpp
    PlexTimeline.cpp)
koalaamp_enable_warnings(koalaamp_plex)
target_include_directories(koalaamp_plex
    PUBLIC "${CMAKE_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_SOURCE_DIR}/third_party/include")
target_link_libraries(koalaamp_plex
    PUBLIC koalaamp_servers koalaamp_library koalaamp_playback
    PRIVATE koalaamp_jellyfin)
