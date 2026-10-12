# SPDX-License-Identifier: MIT
# playlist — PL-1 serialisation of every playlist format a Winamp user's disk
# contains (M3U/M3U8, PLS, B4S, XSPF, ASX/WPL read, playlists.xml index) plus
# autosave. Qt-FREE and engine-FREE by contract (docs/spec.md §2.1, like
# src/audio): it depends only on the C++ standard library. `src/ui` converts
# its own model to/from `playlist::Track` at the window verbs.
add_library(koalaamp_playlist STATIC
    Asx.cpp
    B4s.cpp
    M3u.cpp
    Playlist.cpp
    Playlist.hpp
    Pls.cpp
    Track.hpp
    Util.cpp
    Util.hpp
    Xml.cpp
    Xml.hpp
    Xspf.cpp)
koalaamp_enable_warnings(koalaamp_playlist)
target_include_directories(koalaamp_playlist PUBLIC "${CMAKE_SOURCE_DIR}/src")
