# SPDX-License-Identifier: MIT
# library — track model + scanner + SQLite/FTS5 store (M2).
#
# TagLib is LGPL: system/vcpkg/Homebrew shared library only, never vendored.
# SQLite is public domain; its headers are system includes.
add_library(koalaamp_library STATIC
    LibraryDb.cpp
    LibraryDb.hpp
    Query.cpp
    Query.hpp
    Scanner.cpp
    Scanner.hpp)
koalaamp_enable_warnings(koalaamp_library)
target_include_directories(koalaamp_library PUBLIC "${CMAKE_SOURCE_DIR}/src")

find_package(SQLite3 REQUIRED)

# TagLib: pkg-config on Linux/BSD; vcpkg and Homebrew ship a CMake config
# instead (no pkg-config on a stock Windows runner).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(TAGLIB QUIET taglib)
endif()
if(TAGLIB_FOUND)
    # Third-party headers, included as SYSTEM so warnings apply only to our code.
    target_include_directories(koalaamp_library SYSTEM PRIVATE ${TAGLIB_INCLUDE_DIRS})
    # PUBLIC: consumers of this STATIC library are the final link, and Homebrew's
    # lib directory is not on the default linker path. A PRIVATE link directory
    # never reaches jellyfin_smoke/library_tests, which then fail with `-ltag`.
    target_link_directories(koalaamp_library PUBLIC ${TAGLIB_LIBRARY_DIRS})
    target_link_libraries(koalaamp_library PUBLIC ${TAGLIB_LIBRARIES})
else()
    find_package(TagLib REQUIRED)
    target_link_libraries(koalaamp_library PUBLIC TagLib::TagLib)
endif()
# Some FindTagLib/Homebrew combinations expose only the bare `-ltag` name.
# Keep the discovered directory on the consumer link line as well.
if(TAGLIB_LIBRARY_DIRS)
    target_link_directories(koalaamp_library PUBLIC ${TAGLIB_LIBRARY_DIRS})
endif()
# Homebrew's prefix is not on the default linker path. If the discovery module
# only managed to produce a bare `-ltag`, anchor the link to the actual dylib.
if(APPLE)
    find_library(TAGLIB_ABSOLUTE_LIBRARY NAMES tag
        HINTS ${TAGLIB_LIBRARY_DIRS} /opt/homebrew/lib /usr/local/lib)
    if(TAGLIB_ABSOLUTE_LIBRARY)
        target_link_libraries(koalaamp_library PUBLIC ${TAGLIB_ABSOLUTE_LIBRARY})
    endif()
endif()
target_link_libraries(koalaamp_library PUBLIC SQLite::SQLite3)
