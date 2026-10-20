# SPDX-License-Identifier: MIT
# scrobble — client-side Last.fm/ListenBrainz scrobbling (SC-1). Qt-free.
#
# The production transport adapter wraps the Jellyfin gateway's libcurl
# transport in CurlTransport.cpp only; none of this module's public headers
# include the gateway, and it links no Qt.

add_library(koalaamp_scrobble STATIC
    Md5.cpp
    LastFmClient.cpp
    ListenBrainzClient.cpp
    ScrobbleQueue.cpp
    ScrobbleCoordinator.cpp
    CurlTransport.cpp)
koalaamp_enable_warnings(koalaamp_scrobble)
target_include_directories(koalaamp_scrobble
    PUBLIC "${CMAKE_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_SOURCE_DIR}/third_party/include")

# The curl adapter is an implementation detail: PRIVATE keeps the gateway out of
# consumers' public interface (static-library link propagation is expected).
target_link_libraries(koalaamp_scrobble PRIVATE koalaamp_jellyfin)
