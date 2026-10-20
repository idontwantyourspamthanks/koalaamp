# SPDX-License-Identifier: MIT
add_library(koalaamp_servers STATIC ServerBook.cpp SecretStore.cpp)
target_include_directories(koalaamp_servers
    PUBLIC "${CMAKE_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_SOURCE_DIR}/third_party/include")
target_link_libraries(koalaamp_servers PUBLIC koalaamp_library)

# The access token lives in the OS credential store. Linux speaks Secret Service
# (and KWallet) over Qt DBus; Windows and macOS use the system APIs directly.
if(WIN32)
    target_sources(koalaamp_servers PRIVATE PlatformSecretStore_win.cpp)
    target_link_libraries(koalaamp_servers PUBLIC advapi32)
elseif(APPLE)
    target_sources(koalaamp_servers PRIVATE PlatformSecretStore_mac.cpp)
    target_link_libraries(koalaamp_servers PUBLIC "-framework Security")
else()
    find_package(Qt6 QUIET COMPONENTS DBus)
    if(Qt6DBus_FOUND)
        target_sources(koalaamp_servers PRIVATE PlatformSecretStore_linux.cpp)
        target_link_libraries(koalaamp_servers PUBLIC Qt6::DBus)
    else()
        target_sources(koalaamp_servers PRIVATE PlatformSecretStore_none.cpp)
        message(STATUS "KoalaAmp: Qt DBus missing — Jellyfin sessions stay in memory")
    endif()
endif()

# FE-5 spike: koalapaw Jellyfin gateway port (auth/discovery/browse).
add_subdirectory(jellyfin)
