// SPDX-License-Identifier: MIT
//
// Test-only helpers shared by the SE-1 skins tests: synthetic BMP builders
// (all depths + BI_RLE8), a minimal store-only ZIP writer, and temp-file
// utilities. Nothing here ships in the application.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace skins_test {

inline void put16(std::vector<std::uint8_t>& b, std::size_t off, std::uint16_t v) {
    b[off] = static_cast<std::uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<std::uint8_t>(v >> 8);
}

inline void put32(std::vector<std::uint8_t>& b, std::size_t off, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        b[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
    }
}

/// Grey-scale palette (index i -> RGB(i,i,i)).
inline void writeGreyPalette(std::vector<std::uint8_t>& b, std::size_t off, int count) {
    for (int i = 0; i < count; ++i) {
        b[off + i * 4 + 0] = static_cast<std::uint8_t>(i);
        b[off + i * 4 + 1] = static_cast<std::uint8_t>(i);
        b[off + i * 4 + 2] = static_cast<std::uint8_t>(i);
        b[off + i * 4 + 3] = 0;
    }
}

/// Palettised / direct BMP. `payload` is the raw pixel block (rows bottom-up
/// for the uncompressed depths); `compression` selects BI_RGB (0) or BI_RLE8 (1).
inline std::vector<std::uint8_t> makeBmp(int w, int h, int bpp, std::uint32_t compression,
                                         const std::vector<std::uint8_t>& payload,
                                         bool topDown = false) {
    const int paletteEntries = bpp <= 8 ? (bpp == 8 ? 256 : (1 << bpp)) : 0;
    const std::size_t paletteOff = 14 + 40;
    const std::size_t pixelOff = paletteOff + static_cast<std::size_t>(paletteEntries) * 4;
    std::vector<std::uint8_t> b(pixelOff + payload.size(), 0);
    b[0] = 'B';
    b[1] = 'M';
    put32(b, 2, static_cast<std::uint32_t>(b.size()));
    put32(b, 10, static_cast<std::uint32_t>(pixelOff));
    put32(b, 14, 40);
    put32(b, 18, static_cast<std::uint32_t>(w));
    put32(b, 22, static_cast<std::uint32_t>(topDown ? -h : h));
    put16(b, 26, 1);
    put16(b, 28, static_cast<std::uint16_t>(bpp));
    put32(b, 30, compression);
    put32(b, 34, static_cast<std::uint32_t>(payload.size()));
    if (paletteEntries > 0) {
        writeGreyPalette(b, paletteOff, paletteEntries);
    }
    for (std::size_t i = 0; i < payload.size(); ++i) {
        b[pixelOff + i] = payload[i];
    }
    return b;
}

/// A valid 8-bit BI_RGB sheet of a solid index (enough for loaders/fuzz).
inline std::vector<std::uint8_t> solidSheet(int w, int h, std::uint8_t index) {
    const std::size_t stride = ((static_cast<std::size_t>(8) * static_cast<std::size_t>(w) + 31) / 32) * 4;
    std::vector<std::uint8_t> pixels(stride * static_cast<std::size_t>(h), index);
    return makeBmp(w, h, 8, 0, pixels);
}

struct ZipMember {
    std::string name;
    std::vector<std::uint8_t> data;
};

/// Minimal store-only (method 0) ZIP writer. `truncate` cuts the tail to make a
/// deliberately unparsable archive.
inline std::vector<std::uint8_t> buildZip(const std::vector<ZipMember>& members,
                                         std::size_t truncateBy = 0) {
    std::vector<std::uint8_t> out;
    struct Cd {
        std::size_t offset;
        std::string name;
        std::uint32_t size;
    };
    std::vector<Cd> central;

    for (const auto& m : members) {
        const std::size_t offset = out.size();
        out.resize(offset + 30, 0);
        put32(out, offset + 0, 0x04034b50);
        put16(out, offset + 4, 20);
        put16(out, offset + 8, 0);  // stored
        put32(out, offset + 14, 0); // crc (unchecked by the reader)
        put32(out, offset + 18, static_cast<std::uint32_t>(m.data.size()));
        put32(out, offset + 22, static_cast<std::uint32_t>(m.data.size()));
        put16(out, offset + 26, static_cast<std::uint16_t>(m.name.size()));
        out.insert(out.end(), m.name.begin(), m.name.end());
        out.insert(out.end(), m.data.begin(), m.data.end());
        central.push_back({offset, m.name, static_cast<std::uint32_t>(m.data.size())});
    }

    const std::size_t cdStart = out.size();
    for (const auto& c : central) {
        const std::size_t off = out.size();
        out.resize(off + 46, 0);
        put32(out, off + 0, 0x02014b50);
        put16(out, off + 4, 20);
        put16(out, off + 6, 20);
        put16(out, off + 10, 0);
        put32(out, off + 16, 0);
        put32(out, off + 20, c.size);
        put32(out, off + 24, c.size);
        put16(out, off + 28, static_cast<std::uint16_t>(c.name.size()));
        put32(out, off + 42, static_cast<std::uint32_t>(c.offset));
        out.insert(out.end(), c.name.begin(), c.name.end());
    }
    const std::size_t cdSize = out.size() - cdStart;

    const std::size_t eocd = out.size();
    out.resize(eocd + 22, 0);
    put32(out, eocd + 0, 0x06054b50);
    put16(out, eocd + 8, static_cast<std::uint16_t>(central.size()));
    put16(out, eocd + 10, static_cast<std::uint16_t>(central.size()));
    put32(out, eocd + 12, static_cast<std::uint32_t>(cdSize));
    put32(out, eocd + 16, static_cast<std::uint32_t>(cdStart));

    if (truncateBy >= out.size()) {
        return {};
    }
    out.resize(out.size() - truncateBy);
    return out;
}

/// RAII temp directory (unique per process; removed on destruction).
class TempDir {
public:
    explicit TempDir(std::string_view tag) {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() /
               ("koalaamp-se1-" + std::string(tag) + "-" + std::to_string(++counter));
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::filesystem::path file(std::string_view name, const std::vector<std::uint8_t>& bytes) const {
        const auto p = path / std::string(name);
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        return p;
    }

    std::filesystem::path path;
};

}  // namespace skins_test
