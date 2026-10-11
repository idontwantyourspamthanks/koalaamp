// SPDX-License-Identifier: MIT
#include "skins/BmpDecoder.hpp"

#include <array>
#include <cstring>

#include <QColor>

namespace koalaamp::skins {
namespace {

std::uint16_t rd16(const std::uint8_t *p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t rd32(const std::uint8_t *p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

struct Palette {
    std::array<std::uint32_t, 256> colors{};  // 0x00RRGGBB, already BGRA-swapped.
};

/// Reads `count` BGRA palette quads starting after the header at `off`.
void readPalette(const std::vector<std::uint8_t> &b, std::size_t off, int count, Palette &pal) {
    for (int i = 0; i < count && off + 4 <= b.size(); ++i, off += 4) {
        const std::uint32_t blue = b[off];
        const std::uint32_t green = b[off + 1];
        const std::uint32_t red = b[off + 2];
        pal.colors[static_cast<std::size_t>(i)] = (red << 16) | (green << 8) | blue;
    }
}

constexpr std::uint32_t kBlack = 0x00000000u;

}  // namespace

std::optional<BmpInfo> bmpInfo(const std::vector<std::uint8_t> &b) {
    if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') {
        return std::nullopt;
    }
    const std::uint32_t headerSize = rd32(&b[14]);
    if (headerSize < 40 || 14 + headerSize > b.size()) {
        return std::nullopt;
    }
    BmpInfo info;
    const std::int32_t w = static_cast<std::int32_t>(rd32(&b[18]));
    const std::int32_t h = static_cast<std::int32_t>(rd32(&b[22]));
    info.width = w;
    info.height = h < 0 ? -h : h;
    info.topDown = h < 0;
    info.bitsPerPixel = rd16(&b[28]);
    info.compression = rd32(&b[30]);
    info.pixelOffset = rd32(&b[10]);
    if (info.width <= 0 || info.height <= 0) {
        return std::nullopt;
    }
    // SE-1 hardening: reject absurd declared geometry before anyone allocates for
    // it (classic skin sheets are at most a few hundred pixels per side).
    constexpr int kMaxDimension = 16384;
    if (info.width > kMaxDimension || info.height > kMaxDimension) {
        return std::nullopt;
    }
    return info;
}

std::optional<QImage> decodeBmp(const std::vector<std::uint8_t> &b) {
    const auto info = bmpInfo(b);
    if (!info) {
        return std::nullopt;
    }
    const std::uint32_t headerSize = rd32(&b[14]);
    const int width = info->width;
    const int height = info->height;
    const int bpp = info->bitsPerPixel;

    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull()) {
        return std::nullopt;
    }

    // For BI_RGB the pixel rows are bottom-up unless the height was negative.
    const auto displayRow = [&](int storedRow) {
        return info->topDown ? storedRow : height - 1 - storedRow;
    };

    if (info->compression == 0) {  // BI_RGB
        const std::size_t stride = ((static_cast<std::size_t>(bpp) * width + 31) / 32) * 4;
        const std::size_t pixelBase = info->pixelOffset;
        if (pixelBase + stride * static_cast<std::size_t>(height) > b.size()) {
            return std::nullopt;
        }

        Palette pal;
        if (bpp <= 8) {
            const int paletteCount = bpp == 8 ? 256 : (1 << bpp);
            readPalette(b, 14 + headerSize, paletteCount, pal);
        }

        for (int row = 0; row < height; ++row) {
            const std::uint8_t *src = &b[pixelBase + stride * static_cast<std::size_t>(row)];
            auto *dst = reinterpret_cast<QRgb *>(image.scanLine(displayRow(row)));
            for (int x = 0; x < width; ++x) {
                std::uint32_t rgb = kBlack;
                switch (bpp) {
                case 1:
                    rgb = pal.colors[(src[x / 8] >> (7 - (x % 8))) & 1];
                    break;
                case 4:
                    rgb = pal.colors[(src[x / 2] >> ((x % 2) ? 0 : 4)) & 0x0F];
                    break;
                case 8:
                    rgb = pal.colors[src[x]];
                    break;
                case 24: {
                    const std::uint8_t *p = src + x * 3;
                    rgb = (static_cast<std::uint32_t>(p[2]) << 16) |
                          (static_cast<std::uint32_t>(p[1]) << 8) | p[0];
                    break;
                }
                case 32: {
                    const std::uint8_t *p = src + x * 4;
                    rgb = (static_cast<std::uint32_t>(p[2]) << 16) |
                          (static_cast<std::uint32_t>(p[1]) << 8) | p[0];
                    break;
                }
                default:
                    return std::nullopt;
                }
                dst[x] = static_cast<QRgb>(0xFF000000u | rgb);
            }
        }
        return image;
    }

    if (info->compression == 1) {  // BI_RLE8 (8-bit only)
        if (bpp != 8) {
            return std::nullopt;
        }
        Palette pal;
        readPalette(b, 14 + headerSize, 256, pal);

        // RLE runs may `delta`-skip pixels (notably balance.bmp's spare left
        // columns); the reference decoder (Chrome/Skia) leaves skipped pixels
        // black, so seed the canvas to match.
        image.fill(QColor(0, 0, 0));

        std::size_t i = info->pixelOffset;
        int x = 0;
        int storedRow = 0;
        // RLE rows are stored bottom-up as well.
        while (i + 1 < b.size() && storedRow < height) {
            const std::uint8_t count = b[i++];
            if (count != 0) {
                const std::uint8_t value = b[i++];
                QRgb *dst = nullptr;
                if (storedRow < height) {
                    dst = reinterpret_cast<QRgb *>(image.scanLine(displayRow(storedRow)));
                }
                for (int k = 0; k < count && x < width; ++k, ++x) {
                    if (dst) {
                        dst[x] = static_cast<QRgb>(0xFF000000u | pal.colors[value]);
                    }
                }
            } else {
                const std::uint8_t token = b[i++];
                if (token == 0) {  // end of line
                    x = 0;
                    ++storedRow;
                } else if (token == 1) {  // end of bitmap
                    break;
                } else if (token == 2) {  // delta
                    if (i + 1 >= b.size()) {
                        break;
                    }
                    x += b[i];
                    storedRow += b[i + 1];
                    i += 2;
                } else {  // absolute run of `token` literal indices
                    QRgb *dst = nullptr;
                    if (storedRow < height) {
                        dst = reinterpret_cast<QRgb *>(image.scanLine(displayRow(storedRow)));
                    }
                    for (int k = 0; k < token; ++k) {
                        if (i >= b.size()) {
                            break;
                        }
                        const std::uint8_t index = b[i++];
                        if (dst && x < width) {
                            dst[x] = static_cast<QRgb>(0xFF000000u | pal.colors[index]);
                        }
                        ++x;
                    }
                    if (token & 1) {  // literal runs are word-aligned
                        ++i;
                    }
                }
            }
        }
        return image;
    }

    return std::nullopt;  // BI_RLE4 / BI_BITFIELDS: not needed for classic skins
}

}  // namespace koalaamp::skins
