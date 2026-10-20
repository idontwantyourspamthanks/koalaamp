# SPDX-License-Identifier: MIT
# library — track model + scanner + SQLite/FTS5 store (M2).
#
# TagLib is LGPL: system/vcpkg/Homebrew shared library only, never vendored.
# SQLite is public domain; its headers are system includes.
add_library(koalaamp_library STATIC
    LibraryDb.cpp
    LibraryDb.hpp
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
    target_link_directories(koalaamp_library PRIVATE ${TAGLIB_LIBRARY_DIRS})
    target_link_libraries(koalaamp_library PRIVATE ${TAGLIB_LIBRARIES})
else()
    find_package(TagLib REQUIRED)
    target_link_libraries(koalaamp_library PRIVATE TagLib::TagLib)
endif()
target_link_libraries(koalaamp_library PUBLIC SQLite::SQLite3)
