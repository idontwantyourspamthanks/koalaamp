# SPDX-License-Identifier: MIT
# migration — Winamp import readers (MI-1). Qt-free.
add_library(koalaamp_migration STATIC
    NDE.cpp
    NDE.hpp
    Bookmarks.cpp
    Bookmarks.hpp
    Migration.cpp
    Migration.hpp
    WinampConfig.cpp
    WinampConfig.hpp)
koalaamp_enable_warnings(koalaamp_migration)
target_include_directories(koalaamp_migration PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(koalaamp_migration PRIVATE koalaamp_library)
