# SPDX-License-Identifier: MIT
# jellyfin — koalapaw port spike (FE-5). Static library + smoke binary.
# The JSON DOM (nlohmann) and libcurl pins: docs/spikes/koalapaw-port.md.

add_library(koalaamp_jellyfin STATIC
    Sha256.cpp
    DeviceIdentity.cpp
    AuthHeaders.cpp
    HttpTransport.cpp
    FixtureTransport.cpp
    JellyfinClient.cpp
    ArtworkCache.cpp
    JellyfinStream.cpp
    JellyfinMediaSource.cpp
    JellyfinPlayback.cpp
    JellyfinPlaybackReporter.cpp
    WebSocket.cpp
    JellyfinRemote.cpp
    JellyfinRemotePlayback.cpp
    JellyfinSource.cpp
    Discovery.cpp)
koalaamp_enable_warnings(koalaamp_jellyfin)
target_include_directories(koalaamp_jellyfin
    PUBLIC "${CMAKE_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_SOURCE_DIR}/third_party/include")

find_package(CURL REQUIRED)
target_link_libraries(koalaamp_jellyfin
    PUBLIC koalaamp_library
    PRIVATE CURL::libcurl)

# Raw-socket UDP discovery: Windows needs the winsock import library
# (Linux/macOS have sockets in libc — no explicit link, hence the guard).
if(WIN32)
    target_link_libraries(koalaamp_jellyfin PRIVATE Ws2_32)
endif()

# Live smoke: auth → UDP discovery → album browse against a real server.
add_executable(jellyfin_smoke smoke_jellyfin.cpp)
koalaamp_enable_warnings(jellyfin_smoke)
target_link_libraries(jellyfin_smoke PRIVATE koalaamp_jellyfin)
target_include_directories(jellyfin_smoke PRIVATE "${CMAKE_SOURCE_DIR}/third_party/include")
