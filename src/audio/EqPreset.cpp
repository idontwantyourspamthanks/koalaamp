// SPDX-License-Identifier: MIT
// audio — `.eqf`/`winamp.q1` parse/write implementation. See EqPreset.hpp.

#include "audio/EqPreset.hpp"

#include <algorithm>
#include <cstring>

namespace koalaamp::audio {

namespace {

constexpr char kMagic[] = "Winamp EQ library file v1.1";
constexpr std::size_t kMagicLen = sizeof(kMagic) - 1; // 27
constexpr std::uint8_t kCtrlZ = 0x1A;
constexpr char kTrailer[] = "!--";

const std::vector<EqPreset> kClassic{
    {"Classical", {31, 31, 31, 31, 31, 31, 44, 44, 44, 48}, 31},
    {"Club", {31, 31, 26, 22, 22, 22, 26, 31, 31, 31}, 31},
    {"Dance", {16, 20, 28, 32, 32, 42, 44, 44, 32, 32}, 31},
    {"Laptop speakers/headphones", {24, 14, 23, 38, 36, 29, 24, 16, 11, 8}, 31},
    {"Large hall", {15, 15, 22, 22, 31, 40, 40, 40, 31, 31}, 31},
    {"Party", {20, 20, 31, 31, 31, 31, 31, 31, 20, 20}, 31},
    {"Pop", {35, 24, 20, 19, 23, 34, 36, 36, 35, 35}, 31},
    {"Reggae", {31, 31, 33, 42, 31, 21, 21, 31, 31, 31}, 31},
    {"Rock", {19, 24, 41, 45, 38, 25, 17, 14, 14, 14}, 31},
    {"Soft", {24, 29, 34, 36, 34, 25, 18, 16, 14, 12}, 31},
    {"Ska", {36, 40, 39, 33, 25, 22, 17, 16, 14, 16}, 31},
    {"Full Bass", {16, 16, 16, 22, 29, 39, 46, 49, 50, 50}, 31},
    {"Soft Rock", {25, 25, 28, 33, 39, 41, 38, 33, 27, 17}, 31},
    {"Full Treble", {48, 48, 48, 39, 27, 14, 6, 6, 6, 4}, 31},
    {"Full Bass & Treble", {20, 22, 31, 44, 40, 29, 18, 14, 12, 12}, 31},
    {"Live", {40, 31, 25, 23, 22, 22, 25, 27, 27, 28}, 31},
    {"Techno", {19, 22, 31, 41, 40, 31, 19, 16, 16, 17}, 31},
};

void appendHeader(std::vector<std::uint8_t>& out) {
    out.insert(out.end(), kMagic, kMagic + kMagicLen);
    out.push_back(kCtrlZ);
    out.insert(out.end(), kTrailer, kTrailer + 3);
}

void appendPreset(std::vector<std::uint8_t>& out, const EqPreset& preset) {
    // Name field: at most 256 characters, NUL-terminated within 257 bytes.
    const std::size_t chars = std::min<std::size_t>(preset.name.size(), kEqfNameBytes - 1);
    out.insert(out.end(), preset.name.begin(),
               preset.name.begin() + static_cast<std::ptrdiff_t>(chars));
    out.resize(out.size() + (kEqfNameBytes - chars), 0); // pad to the full name field
    for (int b = 0; b < kEqBands; ++b) {
        out.push_back(static_cast<std::uint8_t>(
            std::clamp(preset.bands[static_cast<std::size_t>(b)], kEqValueMin, kEqValueMax)));
    }
    out.push_back(static_cast<std::uint8_t>(std::clamp(preset.preamp, kEqValueMin, kEqValueMax)));
}

} // namespace

bool isEqfLibrary(const std::uint8_t* data, std::size_t size) noexcept {
    if (data == nullptr || size < kEqfHeaderBytes) {
        return false;
    }
    if (std::memcmp(data, kMagic, kMagicLen) != 0) {
        return false;
    }
    return data[kMagicLen] == kCtrlZ && data[kMagicLen + 1] == '!' && data[kMagicLen + 2] == '-' &&
           data[kMagicLen + 3] == '-';
}

std::vector<EqPreset> parseEqf(const std::uint8_t* data, std::size_t size) {
    std::vector<EqPreset> presets;
    if (!isEqfLibrary(data, size)) {
        return presets;
    }
    std::size_t i = kEqfHeaderBytes;
    while (i + kEqfNameBytes + kEqfValuesBytes <= size) {
        EqPreset preset;
        const std::size_t nameEnd = i + kEqfNameBytes;
        std::size_t nameLen = 0;
        while (i + nameLen < nameEnd && data[i + nameLen] != 0) {
            ++nameLen;
        }
        preset.name.assign(reinterpret_cast<const char*>(data + i), nameLen);
        i = nameEnd;
        for (int b = 0; b < kEqBands; ++b) {
            preset.bands[static_cast<std::size_t>(b)] =
                std::clamp<int>(data[i++], kEqValueMin, kEqValueMax);
        }
        preset.preamp = std::clamp<int>(data[i++], kEqValueMin, kEqValueMax);
        presets.push_back(std::move(preset));
    }
    return presets;
}

std::vector<EqPreset> parseEqf(const std::vector<std::uint8_t>& data) {
    return parseEqf(data.data(), data.size());
}

std::vector<std::uint8_t> writeEqf(const EqPreset& preset) {
    std::vector<std::uint8_t> out;
    out.reserve(kEqfPresetBytes);
    appendHeader(out);
    appendPreset(out, preset);
    return out;
}

std::vector<std::uint8_t> writeEqfLibrary(const std::vector<EqPreset>& presets) {
    std::vector<std::uint8_t> out;
    out.reserve(kEqfHeaderBytes + presets.size() * (kEqfNameBytes + kEqfValuesBytes));
    appendHeader(out);
    for (const EqPreset& preset : presets) {
        appendPreset(out, preset);
    }
    return out;
}

const std::vector<EqPreset>& classicEqPresets() {
    return kClassic;
}

std::vector<EqPreset> importWinampQ1(const std::uint8_t* data, std::size_t size) {
    return parseEqf(data, size);
}

} // namespace koalaamp::audio
