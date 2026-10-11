// SPDX-License-Identifier: MIT
//
// BMP decoder for classic-skin bitmaps (docs/research/skins.md §1.2). Handles
// the depths real skins ship: 1/4/8-bit palettised and 24/32-bit direct, both
// uncompressed (BI_RGB) and the BI_RLE8 run-length encoding that the majority
// of Nullsoft's own base-skin sheets use.
//
// Clean-room: implemented from the public BITMAPINFOHEADER/BI_RLE8 layout.
// Promoted from the FE-3 spike into `src/skins/` by SE-1 (`git mv`, namespace
// `fe3` -> `koalaamp::skins`); SE-1 added the dimension cap in `bmpInfo`.
// BI_RLE4 is deliberately not decoded (outside the classic-skin surface).
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <QImage>

namespace koalaamp::skins {

/// Parsed header facts, useful for diagnostics and format findings.
struct BmpInfo {
    int width = 0;
    int height = 0;          ///< Absolute height as stored (see topDown).
    int bitsPerPixel = 0;
    std::uint32_t compression = 0;  ///< 0 = BI_RGB, 1 = BI_RLE8, ...
    bool topDown = false;    ///< True when the stored height is negative.
    std::size_t pixelOffset = 0;
};

/// Reads only the header; returns nullopt for a non-BMP / truncated buffer.
std::optional<BmpInfo> bmpInfo(const std::vector<std::uint8_t> &bytes);

/// Decodes to a top-left-origin RGB32 QImage (classic skins carry no alpha).
std::optional<QImage> decodeBmp(const std::vector<std::uint8_t> &bytes);

}  // namespace koalaamp::skins
