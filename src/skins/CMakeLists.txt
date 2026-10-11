# SPDX-License-Identifier: MIT
# skins — classic WSZ loading and sprite parsing (docs/spec.md §5).
#
# The RLE8/paletted BMP decoder (BmpDecoder), the ZIP reader (ZipReader) and the
# 212-entry sprite table (SkinSprites.hpp) are the FE-3 spike modules, promoted
# verbatim into this module by SE-1 (docs/tickets/SE-1-wsz-loader.md). SE-1 adds
# the container layer, the lenient text parsers, SkinSet assembly and the
# skin DB.
find_package(ZLIB REQUIRED)

add_library(koalaamp_skins STATIC
    ZipReader.cpp
    BmpDecoder.cpp
    Skin.cpp
    SkinContainer.cpp
    SkinText.cpp
    SkinSet.cpp
    SkinDb.cpp
    SkinLoader.cpp)

koalaamp_enable_warnings(koalaamp_skins)

# Public headers are addressed as "skins/<Header>.hpp" from the app and tests.
target_include_directories(koalaamp_skins
    PUBLIC "${CMAKE_SOURCE_DIR}/src")

# Qt Gui is PUBLIC: Skin.hpp exposes QImage/QPainter, so consumers need Qt.
# zlib is the ZIP inflate backend; it never appears in a public header.
target_link_libraries(koalaamp_skins
    PUBLIC koalaamp_library Qt6::Gui
    PRIVATE ZLIB::ZLIB)
