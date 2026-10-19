# SPDX-License-Identifier: MIT
add_library(koalaamp_servers STATIC ServerBook.cpp)
target_include_directories(koalaamp_servers
    PUBLIC "${CMAKE_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_SOURCE_DIR}/third_party/include")
target_link_libraries(koalaamp_servers PUBLIC koalaamp_library)

# FE-5 spike: koalapaw Jellyfin gateway port (auth/discovery/browse).
add_subdirectory(jellyfin)
