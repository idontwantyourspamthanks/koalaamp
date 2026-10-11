// SPDX-License-Identifier: MIT
// audio — Winamp `.eqf` preset and `winamp.q1` library parse/write.
//
// Container layout (byte-compatible with Winamp), verified against the MIT
// winamp-eqf reference in the Webamp monorepo (packages/winamp-eqf), which in
// turn cites the published format description and the original Winamp author:
//
//   offset 0   31 bytes  "Winamp EQ library file v1.1" + 0x1A + "!--"
//   +31       257 bytes  preset name, NUL-terminated, NUL-padded
//   +288       10 bytes  band sliders, ascending frequency (60 Hz … 16 kHz)
//   +298        1 byte   preamp slider
//              ---------
//              299 bytes  per preset (a `.eqf` is one; `winamp.q1` concatenates
//                         many with no separator)
//
// Slider bytes are Winamp's reversed 6-bit field: 0 = +12 dB, 31 = 0 dB,
// 63 = −12 dB — the same 0..63 scale KoalaAmp exposes (EqValue.hpp). Preset
// name/value data embedded below is the published classic Winamp preset set
// (functional data; provenance: a Winamp `winamp.q1`).

#pragma once

#include "audio/EqValue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace koalaamp::audio {

inline constexpr std::size_t kEqfHeaderBytes = 31;
inline constexpr std::size_t kEqfNameBytes = 257;
inline constexpr std::size_t kEqfValuesBytes = kEqBands + 1;
inline constexpr std::size_t kEqfPresetBytes = kEqfHeaderBytes + kEqfNameBytes + kEqfValuesBytes;

struct EqPreset {
    std::string name;
    std::array<int, kEqBands> bands{
        kEqValueNeutral, kEqValueNeutral, kEqValueNeutral, kEqValueNeutral, kEqValueNeutral,
        kEqValueNeutral, kEqValueNeutral, kEqValueNeutral, kEqValueNeutral, kEqValueNeutral};
    int preamp{kEqValueNeutral};

    friend bool operator==(const EqPreset&, const EqPreset&) = default;
};

/// True when `data` begins with the Winamp EQ library magic.
bool isEqfLibrary(const std::uint8_t* data, std::size_t size) noexcept;

/// Parse a `.eqf` (one preset) or `winamp.q1` (many, concatenated). Returns an
/// empty vector when the magic is missing or the file is too short. Trailing
/// bytes that do not form a whole preset are ignored.
std::vector<EqPreset> parseEqf(const std::uint8_t* data, std::size_t size);
std::vector<EqPreset> parseEqf(const std::vector<std::uint8_t>& data);

/// Serialise one preset as a complete 299-byte `.eqf` file.
std::vector<std::uint8_t> writeEqf(const EqPreset& preset);

/// Serialise a preset library (`winamp.q1` shape): header + every preset.
std::vector<std::uint8_t> writeEqfLibrary(const std::vector<EqPreset>& presets);

/// The classic Winamp preset set (17 presets) as published in `winamp.q1`.
const std::vector<EqPreset>& classicEqPresets();

/// Convenience: import a `winamp.q1`/`.eqf` byte range (alias of parseEqf, named
/// for the migration/import call sites).
std::vector<EqPreset> importWinampQ1(const std::uint8_t* data, std::size_t size);

} // namespace koalaamp::audio
