// SPDX-License-Identifier: MIT
// scrobble — MD5 (RFC 1321). See Md5.hpp.
//
// Public reference algorithm, written from the RFC. No OpenSSL/GCrypt: the only
// consumer is Last.fm's api_sig, which is a compatibility requirement, not a
// security primitive.

#include "scrobble/Md5.hpp"

#include <array>
#include <cstring>

namespace koalaamp::scrobble {
namespace {

constexpr std::array<std::uint32_t, 64> kTable = {
    0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU, 0x4787c62aU,
    0xa8304613U, 0xfd469501U, 0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
    0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U, 0xf61e2562U, 0xc040b340U,
    0x265e5a51U, 0xe9b6c7aaU, 0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
    0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU, 0xa9e3e905U, 0xfcefa3f8U,
    0x676f02d9U, 0x8d2a4c8aU, 0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
    0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U, 0x289b7ec6U, 0xeaa127faU,
    0xd4ef3085U, 0x04881d05U, 0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
    0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U,
    0xffeff47dU, 0x85845dd1U, 0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
    0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U,
};

constexpr std::array<int, 64> kShift = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

std::uint32_t rotateLeft(std::uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

void transform(std::array<std::uint32_t, 4>& state, const std::uint8_t* block) {
    std::array<std::uint32_t, 16> words{};
    for (int i = 0; i < 16; ++i) {
        words[i] = static_cast<std::uint32_t>(block[i * 4]) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 8) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 3]) << 24);
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];

    for (int i = 0; i < 64; ++i) {
        std::uint32_t f = 0;
        int g = 0;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        const std::uint32_t oldD = d;
        d = c;
        c = b;
        b = b + rotateLeft(a + f + kTable[i] + words[g], kShift[i]);
        a = oldD;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

} // namespace

std::array<std::uint8_t, 16> md5(std::string_view input) {
    std::array<std::uint32_t, 4> state = {0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U};

    const auto* data = reinterpret_cast<const std::uint8_t*>(input.data());
    std::size_t remaining = input.size();
    while (remaining >= 64) {
        transform(state, data);
        data += 64;
        remaining -= 64;
    }

    // Padding: 0x80, zeros, then the message length in bits (little-endian).
    std::array<std::uint8_t, 128> tail{};
    if (remaining > 0) {
        std::memcpy(tail.data(), data, remaining);
    }
    tail[remaining] = 0x80;
    const std::size_t tailBlocks = remaining < 56 ? 1 : 2;
    const std::uint64_t bitLength = static_cast<std::uint64_t>(input.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[tailBlocks * 64 - 8 + i] = static_cast<std::uint8_t>(bitLength >> (8 * i));
    }
    for (std::size_t i = 0; i < tailBlocks; ++i) {
        transform(state, tail.data() + i * 64);
    }

    std::array<std::uint8_t, 16> digest{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            digest[i * 4 + j] = static_cast<std::uint8_t>(state[i] >> (8 * j));
        }
    }
    return digest;
}

std::string md5Hex(std::string_view input) {
    static constexpr char kHex[] = "0123456789abcdef";
    const auto digest = md5(input);
    std::string out;
    out.reserve(32);
    for (const std::uint8_t byte : digest) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0f]);
    }
    return out;
}

} // namespace koalaamp::scrobble
