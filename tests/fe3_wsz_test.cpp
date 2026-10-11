// SPDX-License-Identifier: MIT
//
// FE-3 tests: the WSZ/BMP decode path and the ported sprite table. The decoder
// tests are self-contained (synthetic BMPs — no third-party artwork committed);
// the fixture-dependent cases run only when the gitignored base-2.91.wsz has
// been fetched (spikes/fe3_wsz/fetch-skin.sh) and otherwise report a skip.
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "skins/BmpDecoder.hpp"
#include "skins/SkinSprites.hpp"
#include "ui/PlayerRenderer.hpp"
#include "ui/PlayerState.hpp"
#include "skins/Skin.hpp"
#include "skins/SkinSprites.hpp"

namespace {

void put16(std::vector<std::uint8_t> &b, std::size_t off, std::uint16_t v) {
    b[off] = static_cast<std::uint8_t>(v & 0xFF);
    b[off + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(std::vector<std::uint8_t> &b, std::size_t off, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        b[off + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
    }
}

/// A grey-scale 8-bit palette (index i -> RGB(i,i,i)).
void writePalette(std::vector<std::uint8_t> &b, std::size_t off, int count) {
    for (int i = 0; i < count; ++i) {
        b[off + i * 4 + 0] = static_cast<std::uint8_t>(i);  // blue
        b[off + i * 4 + 1] = static_cast<std::uint8_t>(i);  // green
        b[off + i * 4 + 2] = static_cast<std::uint8_t>(i);  // red
        b[off + i * 4 + 3] = 0;
    }
}

/// Builds an 8-bit BMP with the given pixel payload and compression.
std::vector<std::uint8_t> make8BitBmp(int w, int h, std::uint32_t compression,
                                      const std::vector<std::uint8_t> &pixels) {
    const std::size_t paletteOff = 14 + 40;
    const std::size_t pixelOff = paletteOff + 256 * 4;
    std::vector<std::uint8_t> b(pixelOff + pixels.size(), 0);
    b[0] = 'B';
    b[1] = 'M';
    put32(b, 10, static_cast<std::uint32_t>(pixelOff));
    put32(b, 14, 40);
    put32(b, 18, static_cast<std::uint32_t>(w));
    put32(b, 22, static_cast<std::uint32_t>(h));
    put16(b, 26, 1);
    put16(b, 28, 8);
    put32(b, 30, compression);
    put32(b, 34, static_cast<std::uint32_t>(pixels.size()));
    writePalette(b, paletteOff, 256);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        b[pixelOff + i] = pixels[i];
    }
    return b;
}

QImage decodeOrFail(const std::vector<std::uint8_t> &bytes) {
    auto image = koalaamp::skins::decodeBmp(bytes);
    REQUIRE(image.has_value());
    return *image;
}

}  // namespace

TEST_CASE("BMP BI_RGB 8-bit decodes bottom-up with palette", "[fe3][bmp]") {
    // 2x2, stride 4. Stored rows are bottom-up.
    const std::vector<std::uint8_t> pixels = {1, 2, 0, 0, 3, 4, 0, 0};
    const QImage image = decodeOrFail(make8BitBmp(2, 2, 0, pixels));
    REQUIRE(image.size() == QSize(2, 2));
    CHECK(image.pixelColor(0, 0) == QColor(3, 3, 3));  // top row, stored second
    CHECK(image.pixelColor(1, 0) == QColor(4, 4, 4));
    CHECK(image.pixelColor(0, 1) == QColor(1, 1, 1));
    CHECK(image.pixelColor(1, 1) == QColor(2, 2, 2));
}

TEST_CASE("BMP BI_RLE8 decodes runs, EOL and EOB", "[fe3][bmp]") {
    // 4x4, RLE rows bottom-up: runs of index 1,2,3 then 4.
    const std::vector<std::uint8_t> rle = {
        0x04, 0x01, 0x00, 0x00,  // bottom row: 4x index 1
        0x04, 0x02, 0x00, 0x00,
        0x04, 0x03, 0x00, 0x00,
        0x04, 0x04, 0x00, 0x00,
        0x00, 0x01,              // end of bitmap
    };
    const QImage image = decodeOrFail(make8BitBmp(4, 4, 1, rle));
    REQUIRE(image.size() == QSize(4, 4));
    CHECK(image.pixelColor(0, 0) == QColor(4, 4, 4));  // top row
    CHECK(image.pixelColor(3, 3) == QColor(1, 1, 1));  // bottom row
}

TEST_CASE("BMP BI_RLE8 absolute and delta runs", "[fe3][bmp]") {
    // 6x2. Bottom row: 1 pixel index 9, delta(2,0), an absolute run of 3
    // literals {5,6,7} (+1 pad, since the run length is odd), EOL. Top row:
    // EOL only (stays black). Note absolute runs need length >= 3: 0/1/2 are
    // the EOL/EOB/delta tokens.
    const std::vector<std::uint8_t> rle = {
        0x01, 0x09,
        0x00, 0x02, 0x02, 0x00,  // delta x=2
        0x00, 0x03, 0x05, 0x06, 0x07, 0x00,  // absolute 3 pixels + pad
        0x00, 0x00,              // EOL (bottom row done)
        0x00, 0x00,              // EOL (top row done)
        0x00, 0x01,
    };
    const QImage image = decodeOrFail(make8BitBmp(6, 2, 1, rle));
    REQUIRE(image.size() == QSize(6, 2));
    CHECK(image.pixelColor(0, 1) == QColor(9, 9, 9));
    CHECK(image.pixelColor(3, 1) == QColor(5, 5, 5));
    CHECK(image.pixelColor(4, 1) == QColor(6, 6, 6));
    CHECK(image.pixelColor(5, 1) == QColor(7, 7, 7));
    CHECK(image.pixelColor(0, 0) == QColor(0, 0, 0));
}

TEST_CASE("decodeBmp rejects non-BMP input", "[fe3][bmp]") {
    const std::vector<std::uint8_t> junk = {'N', 'O', 'P', 'E'};
    CHECK_FALSE(koalaamp::skins::decodeBmp(junk).has_value());
}

TEST_CASE("sprite table matches the Webamp coordinates", "[fe3][sprites]") {
    using koalaamp::skins::SpriteRect;
    const auto find = [](std::string_view sheet, std::string_view name) -> SpriteRect {
        std::span<const koalaamp::skins::SpriteDef> span;
        if (sheet == "MAIN") span = koalaamp::skins::kSpritesMAIN;
        else if (sheet == "CBUTTONS") span = koalaamp::skins::kSpritesCBUTTONS;
        else if (sheet == "POSBAR") span = koalaamp::skins::kSpritesPOSBAR;
        else if (sheet == "VOLUME") span = koalaamp::skins::kSpritesVOLUME;
        else if (sheet == "BALANCE") span = koalaamp::skins::kSpritesBALANCE;
        else if (sheet == "TITLEBAR") span = koalaamp::skins::kSpritesTITLEBAR;
        else if (sheet == "SHUFREP") span = koalaamp::skins::kSpritesSHUFREP;
        else if (sheet == "MONOSTER") span = koalaamp::skins::kSpritesMONOSTER;
        else if (sheet == "PLAYPAUS") span = koalaamp::skins::kSpritesPLAYPAUS;
        else if (sheet == "NUMBERS") span = koalaamp::skins::kSpritesNUMBERS;
        for (const auto &def : span) {
            if (def.name == name) return def.rect;
        }
        return SpriteRect{-1, -1, 0, 0};
    };

    CHECK(find("MAIN", "MAIN_WINDOW_BACKGROUND") == SpriteRect{0, 0, 275, 116});
    CHECK(find("CBUTTONS", "MAIN_PLAY_BUTTON") == SpriteRect{23, 0, 23, 18});
    CHECK(find("CBUTTONS", "MAIN_PLAY_BUTTON_ACTIVE") == SpriteRect{23, 18, 23, 18});
    CHECK(find("CBUTTONS", "MAIN_NEXT_BUTTON_ACTIVE") == SpriteRect{92, 18, 22, 18});
    CHECK(find("POSBAR", "MAIN_POSITION_SLIDER_BACKGROUND") == SpriteRect{0, 0, 248, 10});
    CHECK(find("POSBAR", "MAIN_POSITION_SLIDER_THUMB") == SpriteRect{248, 0, 29, 10});
    CHECK(find("VOLUME", "MAIN_VOLUME_BACKGROUND") == SpriteRect{0, 0, 68, 420});
    CHECK(find("VOLUME", "MAIN_VOLUME_THUMB") == SpriteRect{15, 422, 14, 11});
    // balance.bmp groove deliberately starts at x=9 (the classic off-by-9 trap).
    CHECK(find("BALANCE", "MAIN_BALANCE_BACKGROUND") == SpriteRect{9, 0, 38, 420});
    CHECK(find("TITLEBAR", "MAIN_TITLE_BAR_SELECTED") == SpriteRect{27, 0, 275, 14});
    CHECK(find("TITLEBAR", "MAIN_CLUTTER_BAR_BACKGROUND") == SpriteRect{304, 0, 8, 43});
    CHECK(find("SHUFREP", "MAIN_EQ_BUTTON_SELECTED") == SpriteRect{0, 73, 23, 12});
    CHECK(find("SHUFREP", "MAIN_SHUFFLE_BUTTON_SELECTED") == SpriteRect{28, 30, 47, 15});
    CHECK(find("MONOSTER", "MAIN_STEREO") == SpriteRect{0, 12, 29, 12});
    CHECK(find("PLAYPAUS", "MAIN_STOPPED_INDICATOR") == SpriteRect{18, 0, 9, 9});
    CHECK(find("NUMBERS", "DIGIT_5") == SpriteRect{45, 0, 9, 13});
}

TEST_CASE("text.bmp font glyph cells use the 5x6 grid", "[fe3][sprites]") {
    // 'a'..'z' occupy row 0; digits/symbols row 1; accented row 2.
    CHECK(koalaamp::skins::fontGlyph(U'a') == koalaamp::skins::SpriteRect{0, 0, 5, 6});
    CHECK(koalaamp::skins::fontGlyph(U'z') == koalaamp::skins::SpriteRect{125, 0, 5, 6});
    CHECK(koalaamp::skins::fontGlyph(U'0') == koalaamp::skins::SpriteRect{0, 6, 5, 6});
    CHECK(koalaamp::skins::fontGlyph(U'?') == koalaamp::skins::SpriteRect{15, 12, 5, 6});
    CHECK_FALSE(koalaamp::skins::fontGlyph(U'\u0001').has_value());
}

TEST_CASE("base-2.91 fixture decodes and renders", "[fe3][fixture]") {
    const std::filesystem::path skinPath = KOALAAMP_FE3_SKIN;
    if (!std::filesystem::exists(skinPath)) {
        SKIP("fixture missing — run spikes/fe3_wsz/fetch-skin.sh");
    }
    const auto skin = koalaamp::skins::Skin::load(skinPath.string());
    REQUIRE(skin.has_value());

    // Sizes must match the decoded header geometry (docs/research/skins.md §1.2).
    REQUIRE(skin->sheet("MAIN") != nullptr);
    CHECK(skin->sheet("MAIN")->size() == QSize(275, 116));
    CHECK(skin->sheet("TITLEBAR")->size() == QSize(344, 87));
    CHECK(skin->sheet("CBUTTONS")->size() == QSize(136, 36));
    CHECK(skin->sheet("VOLUME")->size() == QSize(68, 433));
    CHECK(skin->sheet("POSBAR")->size() == QSize(307, 10));

    // SE-2 superseded the spike's renderer: the harness (and this test) now
    // exercise the app renderer `src/ui` that the product paints with.
    koalaamp::ui::PlayerState state;
    state.title = "winamp 2.91";
    state.volume = 78;
    const QImage rendered = koalaamp::ui::renderMainWindow(*skin, koalaamp::skins::defaultVisColor(),
                                                           state);
    REQUIRE(rendered.size() == QSize(275, 116));
    // The title bar overlay is drawn over the background, so the two must differ
    // somewhere in the top band but agree at the bottom-left corner of main.bmp.
    CHECK(rendered.pixelColor(0, 115) == skin->sheet("MAIN")->pixelColor(0, 115));
    CHECK(rendered.pixelColor(5, 5) == skin->sheet("TITLEBAR")->pixelColor(27 + 5, 5));
}
