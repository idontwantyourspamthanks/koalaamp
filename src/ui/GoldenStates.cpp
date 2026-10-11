// SPDX-License-Identifier: MIT
#include "ui/GoldenStates.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

#include "ui/TitleText.hpp"
#include "ui/VisData.hpp"

namespace koalaamp::ui {

namespace {

/// Base state: Webamp's no-track startup look, which the FE-3 harness pins
/// pixel-for-pixel against the real Webamp (docs/spikes/fe3-wsz-render.md).
PlayerState baseState() {
    PlayerState state;
    state.title = "winamp 2.91";
    state.volume = 78;
    return state;
}

PlayerState playingState() {
    PlayerState state = baseState();
    state.status = PlayerStatus::Playing;
    state.timeVisible = true;
    state.timeSeconds = 65;  // 01:05
    return state;
}

/// A small, self-contained SHA-256 (FIPS 180-4). No dependency, no licensing
/// question, ~50 lines: the manifest only needs a stable digest.
class Sha256 {
public:
    void update(const std::uint8_t* data, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            m_block[m_blockSize++] = data[i];
            if (m_blockSize == 64) {
                processBlock();
                m_blockSize = 0;
                m_bitLength += 512;
            }
        }
    }

    std::string hex() {
        const std::uint64_t bits = m_bitLength + m_blockSize * 8;
        const std::uint8_t padding = m_blockSize < 56 ? 56 - m_blockSize : 120 - m_blockSize;
        const std::uint8_t pad[64] = {0x80};
        update(pad, padding);
        std::uint8_t lengthBytes[8];
        for (int i = 0; i < 8; ++i) {
            lengthBytes[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        }
        update(lengthBytes, 8);
        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (const std::uint32_t word : m_state) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                out.push_back(digits[(word >> shift) & 0xF]);
            }
        }
        return out;
    }

private:
    static std::uint32_t rotate(std::uint32_t value, int amount) {
        return (value >> amount) | (value << (32 - amount));
    }

    void processBlock() {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(m_block[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(m_block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(m_block[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(m_block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 =
                rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = m_state[0];
        std::uint32_t b = m_state[1];
        std::uint32_t c = m_state[2];
        std::uint32_t d = m_state[3];
        std::uint32_t e = m_state[4];
        std::uint32_t f = m_state[5];
        std::uint32_t g = m_state[6];
        std::uint32_t h = m_state[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    std::array<std::uint32_t, 8> m_state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint8_t m_block[64]{};
    std::size_t m_blockSize = 0;
    std::uint64_t m_bitLength = 0;
};

}  // namespace

std::string frameHash(const QImage& image) {
    if (image.isNull()) {
        return {};
    }
    // Normalised to RGB triples so the digest does not depend on the QImage
    // format's byte order.
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    Sha256 sha;
    std::array<std::uint8_t, 3> pixel{};
    for (int y = 0; y < rgb.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            pixel[0] = static_cast<std::uint8_t>(qRed(line[x]));
            pixel[1] = static_cast<std::uint8_t>(qGreen(line[x]));
            pixel[2] = static_cast<std::uint8_t>(qBlue(line[x]));
            sha.update(pixel.data(), pixel.size());
        }
    }
    return sha.hex();
}

const std::vector<std::pair<std::string, PlayerState>>& goldenStateMatrix() {
    static const std::vector<std::pair<std::string, PlayerState>> states = [] {
        std::vector<std::pair<std::string, PlayerState>> out;
        const auto add = [&out](std::string name, const PlayerState& state) {
            out.emplace_back(std::move(name), state);
        };

        add("default", baseState());
        {
            PlayerState state = baseState();
            state.shuffle = true;
            state.repeat = true;
            state.active.play = true;  // transport button held down
            add("toggled", state);
        }
        {
            PlayerState state = baseState();
            state.selected = false;  // inactive window
            add("inactive", state);
        }
        {
            PlayerState state = baseState();
            state.llama = true;  // easter-egg title bar
            add("llama", state);
        }
        {
            PlayerState state = baseState();
            state.pressed.option = true;
            state.pressed.minimize = true;
            state.pressed.shade = true;
            state.pressed.close = true;
            add("titlebar_pressed", state);
        }
        {
            PlayerState state = baseState();
            state.clutter.o = true;
            state.clutter.a = true;
            state.clutter.i = true;
            state.clutter.d = true;
            state.clutter.v = true;
            add("clutter_all", state);
        }
        {
            PlayerState state = baseState();
            state.clutter.enabled = false;
            add("clutter_disabled", state);
        }
        {
            PlayerState state = baseState();
            state.eqSelected = false;
            state.plSelected = false;
            add("windows_off", state);
        }
        {
            PlayerState state = baseState();
            state.eqDepressed = true;
            state.plDepressed = true;
            add("windows_pressed", state);
        }
        {
            PlayerState state = baseState();
            state.shuffle = true;
            state.shuffleDepressed = true;
            state.repeat = true;
            state.repeatDepressed = true;
            add("shuffle_repeat_pressed", state);
        }
        add("playing", playingState());
        {
            PlayerState state = playingState();
            state.working = true;
            add("playing_working", state);
        }
        {
            PlayerState state = playingState();
            state.status = PlayerStatus::Paused;
            add("paused", state);
        }
        {
            PlayerState state = baseState();  // stopped with a track loaded
            state.timeVisible = false;
            add("time_hidden", state);
        }
        {
            PlayerState state = playingState();
            state.countdown = true;
            add("countdown", state);
        }
        for (const int volume : {0, 50, 100}) {
            PlayerState state = baseState();
            state.volume = volume;
            add("volume_" + std::to_string(volume), state);
        }
        {
            PlayerState state = baseState();
            state.volume = 78;
            state.volumeActive = true;
            add("volume_dragging", state);
        }
        for (const int balance : {-100, -50, 0, 50, 100}) {
            PlayerState state = baseState();
            state.balance = balance;
            add("balance_" + std::to_string(balance), state);
        }
        {
            PlayerState state = baseState();
            state.balanceActive = true;
            add("balance_dragging", state);
        }
        for (const int position : {0, 25, 50, 75, 100}) {
            PlayerState state = playingState();
            state.timeSeconds = position * 3;
            state.position = position;
            add("position_" + std::to_string(position), state);
        }
        {
            PlayerState state = playingState();
            state.position = 50;
            state.positionActive = true;
            add("position_dragging", state);
        }
        {
            PlayerState state = baseState();
            state.shade = true;
            add("shade", state);
        }
        {
            PlayerState state = baseState();
            state.shade = true;
            state.active.play = true;
            add("shade_pressed", state);
        }
        {
            PlayerState state = baseState();
            state.shade = true;
            state.status = PlayerStatus::Playing;
            state.timeVisible = true;
            state.timeSeconds = 65;
            state.position = 50;
            add("shade_playing", state);
        }
        {
            PlayerState state = baseState();
            state.shade = true;
            state.selected = false;
            add("shade_inactive", state);
        }
        {
            PlayerState state = playingState();
            state.channels = 2;
            state.khz = 44;
            state.khzKnown = true;
            state.kbps = 128;
            state.kbpsKnown = true;
            state.title = "koalaamp - track one";
            state.visFrame = syntheticVisFrame(3);
            add("spectrum", state);
        }
        {
            PlayerState state = playingState();
            state.channels = 2;
            state.khz = 44;
            state.khzKnown = true;
            state.kbps = 128;
            state.kbpsKnown = true;
            state.title = "koalaamp - track one";
            state.vis = VisMode::Oscilloscope;
            state.visFrame = syntheticVisFrame(5);
            add("oscilloscope", state);
        }
        {
            PlayerState state = playingState();
            state.vis = VisMode::Off;
            state.visFrame.clear();
            add("vis_off", state);
        }
        {
            PlayerState state = playingState();
            state.channels = 1;
            state.khz = 22;
            state.khzKnown = true;
            state.kbps = 64;
            state.kbpsKnown = true;
            add("mono_64kbps", state);
        }
        {
            // A long title scrolls: the doubled form with the separator, one
            // cell into the scroll (Webamp's `marqueeStep` 1).
            PlayerState state = playingState();
            state.title = "01. artist - a very long track title";
            state.marqueeOffsetPx = marqueeScrollPx(state.title, 1);
            add("marquee_scrolled", state);
        }
        {
            PlayerState state = baseState();
            state.title = "AB#\xC3\x85\xC3\x96\xC3\x84" + std::string("\x01", 1) + "?*!@";
            add("font_fallbacks", state);
        }
        return out;
    }();
    return states;
}

const PlayerState* goldenState(std::string_view name) {
    for (const auto& [stateName, state] : goldenStateMatrix()) {
        if (stateName == name) {
            return &state;
        }
    }
    return nullptr;
}

}  // namespace koalaamp::ui
