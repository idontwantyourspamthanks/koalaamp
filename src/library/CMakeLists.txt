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
find_package(PkgConfig REQUIRED)
pkg_check_modules(TAGLIB REQUIRED taglib)

# TagLib headers are third-party: include them as SYSTEM so our warnings apply
# only to our own code.
target_include_directories(koalaamp_library SYSTEM PRIVATE ${TAGLIB_INCLUDE_DIRS})
target_link_directories(koalaamp_library PRIVATE ${TAGLIB_LIBRARY_DIRS})
target_link_libraries(koalaamp_library
    PUBLIC SQLite::SQLite3
    PRIVATE ${TAGLIB_LIBRARIES})
