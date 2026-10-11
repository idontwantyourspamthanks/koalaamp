// SPDX-License-Identifier: MIT
#include "skins/ZipReader.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include <zlib.h>

namespace koalaamp::skins {
namespace {

std::uint16_t rd16(const std::uint8_t *p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t rd32(const std::uint8_t *p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::string lowerBasename(const std::string &name) {
    std::string base = name;
    const auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos) {
        base = base.substr(slash + 1);
    }
    std::transform(base.begin(), base.end(), base.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return base;
}

/// Inflates a raw DEFLATE stream to `expectedSize` bytes.
std::optional<std::vector<std::uint8_t>> inflateRaw(const std::uint8_t *src, std::size_t srcLen,
                                                    std::size_t expectedSize) {
    std::vector<std::uint8_t> out(expectedSize);
    z_stream zs{};
    // -MAX_WBITS => raw deflate, no zlib header.
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        return std::nullopt;
    }
    zs.next_in = const_cast<Bytef *>(src);
    zs.avail_in = static_cast<uInt>(srcLen);
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(out.size());
    const int rc = inflate(&zs, Z_FINISH);
    const std::size_t produced = out.size() - zs.avail_out;
    inflateEnd(&zs);
    if (rc != Z_STREAM_END && rc != Z_OK && rc != Z_BUF_ERROR) {
        return std::nullopt;
    }
    out.resize(produced);
    return out;
}

}  // namespace

std::optional<std::vector<ZipEntry>> readZip(const std::vector<std::uint8_t> &bytes) {
    constexpr std::uint32_t kEocdSig = 0x06054b50;
    constexpr std::uint32_t kCentralSig = 0x02014b50;
    constexpr std::uint32_t kLocalSig = 0x04034b50;

    if (bytes.size() < 22) {
        return std::nullopt;
    }
    // Locate the End Of Central Directory record, scanning backwards (comment
    // may follow it, up to 64 KiB).
    const std::size_t maxComment = 65535;
    std::size_t eocd = std::string::npos;
    const std::size_t scanFrom = bytes.size() > maxComment + 22 ? bytes.size() - (maxComment + 22) : 0;
    for (std::size_t i = bytes.size() - 22 + 1; i-- > scanFrom;) {
        if (rd32(&bytes[i]) == kEocdSig) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) {
        return std::nullopt;
    }

    const std::uint16_t count = rd16(&bytes[eocd + 10]);
    const std::uint32_t cdOffset = rd32(&bytes[eocd + 16]);

    // SE-1 hardening for the fuzz target: bound the member count and refuse
    // offsets that point outside the buffer before walking the directory.
    constexpr std::uint16_t kMaxEntries = 8192;
    constexpr std::uint32_t kMaxMemberBytes = 32u * 1024u * 1024u;
    if (count > kMaxEntries || cdOffset >= bytes.size()) {
        return std::nullopt;
    }

    std::vector<ZipEntry> entries;
    entries.reserve(count);
    std::size_t p = cdOffset;
    for (std::uint16_t n = 0; n < count; ++n) {
        if (p + 46 > bytes.size() || rd32(&bytes[p]) != kCentralSig) {
            return std::nullopt;
        }
        const std::uint16_t method = rd16(&bytes[p + 10]);
        const std::uint32_t compSize = rd32(&bytes[p + 20]);
        const std::uint16_t nameLen = rd16(&bytes[p + 28]);
        const std::uint16_t extraLen = rd16(&bytes[p + 30]);
        const std::uint16_t commentLen = rd16(&bytes[p + 32]);
        const std::uint32_t localOffset = rd32(&bytes[p + 42]);
        if (p + 46 + nameLen > bytes.size()) {
            return std::nullopt;
        }
        std::string name(reinterpret_cast<const char *>(&bytes[p + 46]), nameLen);

        if (localOffset + 30 > bytes.size() || rd32(&bytes[localOffset]) != kLocalSig) {
            return std::nullopt;
        }
        const std::uint16_t lNameLen = rd16(&bytes[localOffset + 26]);
        const std::uint16_t lExtraLen = rd16(&bytes[localOffset + 28]);
        const std::size_t dataStart = localOffset + 30 + lNameLen + lExtraLen;
        // The central directory is authoritative: local headers may carry 0
        // sizes when the archive streams with a data descriptor.
        std::uint32_t uncompSize = rd32(&bytes[p + 24]);
        if (uncompSize == 0 && compSize != 0) {
            uncompSize = rd32(&bytes[localOffset + 22]);
        }
        if (dataStart > bytes.size() || dataStart + compSize > bytes.size()) {
            return std::nullopt;
        }
        if (uncompSize > kMaxMemberBytes) {
            return std::nullopt;  // zip-bomb guard
        }

        ZipEntry entry;
        entry.name = name;
        entry.basename = lowerBasename(name);
        if (method == 0) {
            entry.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(dataStart),
                              bytes.begin() + static_cast<std::ptrdiff_t>(dataStart + compSize));
        } else if (method == 8) {
            auto inflated = inflateRaw(&bytes[dataStart], compSize, uncompSize);
            if (!inflated) {
                return std::nullopt;
            }
            entry.data = std::move(*inflated);
        } else {
            return std::nullopt;  // unsupported method
        }
        entries.push_back(std::move(entry));
        p += 46 + nameLen + extraLen + commentLen;
    }
    return entries;
}

std::optional<std::vector<ZipEntry>> readZipFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    return readZip(bytes);
}

}  // namespace koalaamp::skins
