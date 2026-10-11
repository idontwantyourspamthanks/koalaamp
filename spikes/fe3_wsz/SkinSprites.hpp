// SPDX-License-Identifier: MIT
//
// Sprite coordinate table for classic Winamp skins.
//
// Data ported from Webamp's `skinSprites.ts` (MIT), pinned at
// webamp@e6283c0eabe9b1313cd41df16d2875e897719696 (2026-10-10).
// Webamp: https://github.com/captbaritone/webamp — attribution in LICENSES.md.
//
// The table is *format data* (fixed sprite rectangles baked into Winamp), not
// artwork. FONT_LOOKUP is the 5x6 `text.bmp` bitmap-font grid (31x3 cells);
// the FE-3 spike ported the full main-window set and left the remaining
// sheets (EQ/PLEDIT/GEN) in place for SE-1 to consume unchanged.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace fe3 {

struct SpriteRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool operator==(const SpriteRect &) const = default;
};

struct SpriteDef {
    std::string_view name;
    SpriteRect rect;
};

inline constexpr int kCharWidth = 5;   // text.bmp cell width  (Webamp CHAR_X)
inline constexpr int kCharHeight = 6;  // text.bmp cell height (Webamp CHAR_Y)

// text.bmp glyph cell for a Unicode codepoint: column * 5, row * 6.
inline std::optional<SpriteRect> fontGlyph(char32_t cp) {
    switch (cp) {
        case U'a': return SpriteRect{0, 0, 5, 6};
        case U'b': return SpriteRect{5, 0, 5, 6};
        case U'c': return SpriteRect{10, 0, 5, 6};
        case U'd': return SpriteRect{15, 0, 5, 6};
        case U'e': return SpriteRect{20, 0, 5, 6};
        case U'f': return SpriteRect{25, 0, 5, 6};
        case U'g': return SpriteRect{30, 0, 5, 6};
        case U'h': return SpriteRect{35, 0, 5, 6};
        case U'i': return SpriteRect{40, 0, 5, 6};
        case U'j': return SpriteRect{45, 0, 5, 6};
        case U'k': return SpriteRect{50, 0, 5, 6};
        case U'l': return SpriteRect{55, 0, 5, 6};
        case U'm': return SpriteRect{60, 0, 5, 6};
        case U'n': return SpriteRect{65, 0, 5, 6};
        case U'o': return SpriteRect{70, 0, 5, 6};
        case U'p': return SpriteRect{75, 0, 5, 6};
        case U'q': return SpriteRect{80, 0, 5, 6};
        case U'r': return SpriteRect{85, 0, 5, 6};
        case U's': return SpriteRect{90, 0, 5, 6};
        case U't': return SpriteRect{95, 0, 5, 6};
        case U'u': return SpriteRect{100, 0, 5, 6};
        case U'v': return SpriteRect{105, 0, 5, 6};
        case U'w': return SpriteRect{110, 0, 5, 6};
        case U'x': return SpriteRect{115, 0, 5, 6};
        case U'y': return SpriteRect{120, 0, 5, 6};
        case U'z': return SpriteRect{125, 0, 5, 6};
        case U'"': return SpriteRect{130, 0, 5, 6};
        case U'@': return SpriteRect{135, 0, 5, 6};
        case U' ': return SpriteRect{150, 0, 5, 6};
        case U'0': return SpriteRect{0, 6, 5, 6};
        case U'1': return SpriteRect{5, 6, 5, 6};
        case U'2': return SpriteRect{10, 6, 5, 6};
        case U'3': return SpriteRect{15, 6, 5, 6};
        case U'4': return SpriteRect{20, 6, 5, 6};
        case U'5': return SpriteRect{25, 6, 5, 6};
        case U'6': return SpriteRect{30, 6, 5, 6};
        case U'7': return SpriteRect{35, 6, 5, 6};
        case U'8': return SpriteRect{40, 6, 5, 6};
        case U'9': return SpriteRect{45, 6, 5, 6};
        case U'…': return SpriteRect{50, 6, 5, 6};
        case U'.': return SpriteRect{55, 6, 5, 6};
        case U':': return SpriteRect{60, 6, 5, 6};
        case U'(': return SpriteRect{65, 6, 5, 6};
        case U')': return SpriteRect{70, 6, 5, 6};
        case U'-': return SpriteRect{75, 6, 5, 6};
        case U'\'': return SpriteRect{80, 6, 5, 6};
        case U'!': return SpriteRect{85, 6, 5, 6};
        case U'_': return SpriteRect{90, 6, 5, 6};
        case U'+': return SpriteRect{95, 6, 5, 6};
        case U'\\': return SpriteRect{100, 6, 5, 6};
        case U'/': return SpriteRect{105, 6, 5, 6};
        case U'[': return SpriteRect{110, 6, 5, 6};
        case U']': return SpriteRect{115, 6, 5, 6};
        case U'^': return SpriteRect{120, 6, 5, 6};
        case U'&': return SpriteRect{125, 6, 5, 6};
        case U'%': return SpriteRect{130, 6, 5, 6};
        case U',': return SpriteRect{135, 6, 5, 6};
        case U'=': return SpriteRect{140, 6, 5, 6};
        case U'$': return SpriteRect{145, 6, 5, 6};
        case U'#': return SpriteRect{150, 6, 5, 6};
        case U'Å': return SpriteRect{0, 12, 5, 6};
        case U'Ö': return SpriteRect{5, 12, 5, 6};
        case U'Ä': return SpriteRect{10, 12, 5, 6};
        case U'?': return SpriteRect{15, 12, 5, 6};
        case U'*': return SpriteRect{20, 12, 5, 6};
        case U'<': return SpriteRect{110, 6, 5, 6};
        case U'>': return SpriteRect{115, 6, 5, 6};
        case U'{': return SpriteRect{110, 6, 5, 6};
        case U'}': return SpriteRect{115, 6, 5, 6};
        default: return std::nullopt;
    }
}

// --- BALANCE.BMP ---
inline constexpr std::array<SpriteDef, 3> kSpritesBALANCE = {{
    {"MAIN_BALANCE_BACKGROUND", {9, 0, 38, 420}},
    {"MAIN_BALANCE_THUMB", {15, 422, 14, 11}},
    {"MAIN_BALANCE_THUMB_ACTIVE", {0, 422, 14, 11}},
}};

// --- CBUTTONS.BMP ---
inline constexpr std::array<SpriteDef, 12> kSpritesCBUTTONS = {{
    {"MAIN_PREVIOUS_BUTTON", {0, 0, 23, 18}},
    {"MAIN_PREVIOUS_BUTTON_ACTIVE", {0, 18, 23, 18}},
    {"MAIN_PLAY_BUTTON", {23, 0, 23, 18}},
    {"MAIN_PLAY_BUTTON_ACTIVE", {23, 18, 23, 18}},
    {"MAIN_PAUSE_BUTTON", {46, 0, 23, 18}},
    {"MAIN_PAUSE_BUTTON_ACTIVE", {46, 18, 23, 18}},
    {"MAIN_STOP_BUTTON", {69, 0, 23, 18}},
    {"MAIN_STOP_BUTTON_ACTIVE", {69, 18, 23, 18}},
    {"MAIN_NEXT_BUTTON", {92, 0, 23, 18}},
    {"MAIN_NEXT_BUTTON_ACTIVE", {92, 18, 22, 18}},
    {"MAIN_EJECT_BUTTON", {114, 0, 22, 16}},
    {"MAIN_EJECT_BUTTON_ACTIVE", {114, 16, 22, 16}},
}};

// --- MAIN.BMP ---
inline constexpr std::array<SpriteDef, 1> kSpritesMAIN = {{
    {"MAIN_WINDOW_BACKGROUND", {0, 0, 275, 116}},
}};

// --- MONOSTER.BMP ---
inline constexpr std::array<SpriteDef, 4> kSpritesMONOSTER = {{
    {"MAIN_STEREO", {0, 12, 29, 12}},
    {"MAIN_STEREO_SELECTED", {0, 0, 29, 12}},
    {"MAIN_MONO", {29, 12, 27, 12}},
    {"MAIN_MONO_SELECTED", {29, 0, 27, 12}},
}};

// --- NUMBERS.BMP ---
inline constexpr std::array<SpriteDef, 12> kSpritesNUMBERS = {{
    {"NO_MINUS_SIGN", {9, 6, 5, 1}},
    {"MINUS_SIGN", {20, 6, 5, 1}},
    {"DIGIT_0", {0, 0, 9, 13}},
    {"DIGIT_1", {9, 0, 9, 13}},
    {"DIGIT_2", {18, 0, 9, 13}},
    {"DIGIT_3", {27, 0, 9, 13}},
    {"DIGIT_4", {36, 0, 9, 13}},
    {"DIGIT_5", {45, 0, 9, 13}},
    {"DIGIT_6", {54, 0, 9, 13}},
    {"DIGIT_7", {63, 0, 9, 13}},
    {"DIGIT_8", {72, 0, 9, 13}},
    {"DIGIT_9", {81, 0, 9, 13}},
}};

// --- NUMS_EX.BMP ---
inline constexpr std::array<SpriteDef, 12> kSpritesNUMS_EX = {{
    {"NO_MINUS_SIGN_EX", {90, 0, 9, 13}},
    {"MINUS_SIGN_EX", {99, 0, 9, 13}},
    {"DIGIT_0_EX", {0, 0, 9, 13}},
    {"DIGIT_1_EX", {9, 0, 9, 13}},
    {"DIGIT_2_EX", {18, 0, 9, 13}},
    {"DIGIT_3_EX", {27, 0, 9, 13}},
    {"DIGIT_4_EX", {36, 0, 9, 13}},
    {"DIGIT_5_EX", {45, 0, 9, 13}},
    {"DIGIT_6_EX", {54, 0, 9, 13}},
    {"DIGIT_7_EX", {63, 0, 9, 13}},
    {"DIGIT_8_EX", {72, 0, 9, 13}},
    {"DIGIT_9_EX", {81, 0, 9, 13}},
}};

// --- PLAYPAUS.BMP ---
inline constexpr std::array<SpriteDef, 5> kSpritesPLAYPAUS = {{
    {"MAIN_PLAYING_INDICATOR", {0, 0, 9, 9}},
    {"MAIN_PAUSED_INDICATOR", {9, 0, 9, 9}},
    {"MAIN_STOPPED_INDICATOR", {18, 0, 9, 9}},
    {"MAIN_NOT_WORKING_INDICATOR", {36, 0, 9, 9}},
    {"MAIN_WORKING_INDICATOR", {39, 0, 9, 9}},
}};

// --- POSBAR.BMP ---
inline constexpr std::array<SpriteDef, 3> kSpritesPOSBAR = {{
    {"MAIN_POSITION_SLIDER_BACKGROUND", {0, 0, 248, 10}},
    {"MAIN_POSITION_SLIDER_THUMB", {248, 0, 29, 10}},
    {"MAIN_POSITION_SLIDER_THUMB_SELECTED", {278, 0, 29, 10}},
}};

// --- SHUFREP.BMP ---
inline constexpr std::array<SpriteDef, 16> kSpritesSHUFREP = {{
    {"MAIN_SHUFFLE_BUTTON", {28, 0, 47, 15}},
    {"MAIN_SHUFFLE_BUTTON_DEPRESSED", {28, 15, 47, 15}},
    {"MAIN_SHUFFLE_BUTTON_SELECTED", {28, 30, 47, 15}},
    {"MAIN_SHUFFLE_BUTTON_SELECTED_DEPRESSED", {28, 45, 47, 15}},
    {"MAIN_REPEAT_BUTTON", {0, 0, 28, 15}},
    {"MAIN_REPEAT_BUTTON_DEPRESSED", {0, 15, 28, 15}},
    {"MAIN_REPEAT_BUTTON_SELECTED", {0, 30, 28, 15}},
    {"MAIN_REPEAT_BUTTON_SELECTED_DEPRESSED", {0, 45, 28, 15}},
    {"MAIN_EQ_BUTTON", {0, 61, 23, 12}},
    {"MAIN_EQ_BUTTON_SELECTED", {0, 73, 23, 12}},
    {"MAIN_EQ_BUTTON_DEPRESSED", {46, 61, 23, 12}},
    {"MAIN_EQ_BUTTON_DEPRESSED_SELECTED", {46, 73, 23, 12}},
    {"MAIN_PLAYLIST_BUTTON", {23, 61, 23, 12}},
    {"MAIN_PLAYLIST_BUTTON_SELECTED", {23, 73, 23, 12}},
    {"MAIN_PLAYLIST_BUTTON_DEPRESSED", {69, 61, 23, 12}},
    {"MAIN_PLAYLIST_BUTTON_DEPRESSED_SELECTED", {69, 73, 23, 12}},
}};

// --- TITLEBAR.BMP ---
inline constexpr std::array<SpriteDef, 27> kSpritesTITLEBAR = {{
    {"MAIN_TITLE_BAR", {27, 15, 275, 14}},
    {"MAIN_TITLE_BAR_SELECTED", {27, 0, 275, 14}},
    {"MAIN_EASTER_EGG_TITLE_BAR", {27, 72, 275, 14}},
    {"MAIN_EASTER_EGG_TITLE_BAR_SELECTED", {27, 57, 275, 14}},
    {"MAIN_OPTIONS_BUTTON", {0, 0, 9, 9}},
    {"MAIN_OPTIONS_BUTTON_DEPRESSED", {0, 9, 9, 9}},
    {"MAIN_MINIMIZE_BUTTON", {9, 0, 9, 9}},
    {"MAIN_MINIMIZE_BUTTON_DEPRESSED", {9, 9, 9, 9}},
    {"MAIN_SHADE_BUTTON", {0, 18, 9, 9}},
    {"MAIN_SHADE_BUTTON_DEPRESSED", {9, 18, 9, 9}},
    {"MAIN_CLOSE_BUTTON", {18, 0, 9, 9}},
    {"MAIN_CLOSE_BUTTON_DEPRESSED", {18, 9, 9, 9}},
    {"MAIN_CLUTTER_BAR_BACKGROUND", {304, 0, 8, 43}},
    {"MAIN_CLUTTER_BAR_BACKGROUND_DISABLED", {312, 0, 8, 43}},
    {"MAIN_CLUTTER_BAR_BUTTON_O_SELECTED", {304, 47, 8, 8}},
    {"MAIN_CLUTTER_BAR_BUTTON_A_SELECTED", {312, 55, 8, 7}},
    {"MAIN_CLUTTER_BAR_BUTTON_I_SELECTED", {320, 62, 8, 7}},
    {"MAIN_CLUTTER_BAR_BUTTON_D_SELECTED", {328, 69, 8, 8}},
    {"MAIN_CLUTTER_BAR_BUTTON_V_SELECTED", {336, 77, 8, 7}},
    {"MAIN_SHADE_BACKGROUND", {27, 42, 275, 14}},
    {"MAIN_SHADE_BACKGROUND_SELECTED", {27, 29, 275, 14}},
    {"MAIN_SHADE_BUTTON_SELECTED", {0, 27, 9, 9}},
    {"MAIN_SHADE_BUTTON_SELECTED_DEPRESSED", {9, 27, 9, 9}},
    {"MAIN_SHADE_POSITION_BACKGROUND", {0, 36, 17, 7}},
    {"MAIN_SHADE_POSITION_THUMB", {20, 36, 3, 7}},
    {"MAIN_SHADE_POSITION_THUMB_LEFT", {17, 36, 3, 7}},
    {"MAIN_SHADE_POSITION_THUMB_RIGHT", {23, 36, 3, 7}},
}};

// --- VOLUME.BMP ---
inline constexpr std::array<SpriteDef, 3> kSpritesVOLUME = {{
    {"MAIN_VOLUME_BACKGROUND", {0, 0, 68, 420}},
    {"MAIN_VOLUME_THUMB", {15, 422, 14, 11}},
    {"MAIN_VOLUME_THUMB_SELECTED", {0, 422, 14, 11}},
}};

// --- EQ_EX.BMP ---
inline constexpr std::array<SpriteDef, 12> kSpritesEQ_EX = {{
    {"EQ_SHADE_BACKGROUND_SELECTED", {0, 0, 275, 14}},
    {"EQ_SHADE_BACKGROUND", {0, 15, 275, 14}},
    {"EQ_SHADE_VOLUME_SLIDER_LEFT", {1, 30, 3, 7}},
    {"EQ_SHADE_VOLUME_SLIDER_CENTER", {4, 30, 3, 7}},
    {"EQ_SHADE_VOLUME_SLIDER_RIGHT", {7, 30, 3, 7}},
    {"EQ_SHADE_BALANCE_SLIDER_LEFT", {11, 30, 3, 7}},
    {"EQ_SHADE_BALANCE_SLIDER_CENTER", {14, 30, 3, 7}},
    {"EQ_SHADE_BALANCE_SLIDER_RIGHT", {17, 30, 3, 7}},
    {"EQ_MAXIMIZE_BUTTON_ACTIVE", {1, 38, 9, 9}},
    {"EQ_MINIMIZE_BUTTON_ACTIVE", {1, 47, 9, 9}},
    {"EQ_SHADE_CLOSE_BUTTON", {11, 38, 9, 9}},
    {"EQ_SHADE_CLOSE_BUTTON_ACTIVE", {11, 47, 9, 9}},
}};

// --- EQMAIN.BMP ---
inline constexpr std::array<SpriteDef, 22> kSpritesEQMAIN = {{
    {"EQ_WINDOW_BACKGROUND", {0, 0, 275, 116}},
    {"EQ_TITLE_BAR", {0, 149, 275, 14}},
    {"EQ_TITLE_BAR_SELECTED", {0, 134, 275, 14}},
    {"EQ_SLIDER_BACKGROUND", {13, 164, 209, 129}},
    {"EQ_SLIDER_THUMB", {0, 164, 11, 11}},
    {"EQ_SLIDER_THUMB_SELECTED", {0, 176, 11, 11}},
    {"EQ_CLOSE_BUTTON", {0, 116, 9, 9}},
    {"EQ_CLOSE_BUTTON_ACTIVE", {0, 125, 9, 9}},
    {"EQ_MAXIMIZE_BUTTON_ACTIVE_FALLBACK", {254, 152, 9, 9}},
    {"EQ_ON_BUTTON", {10, 119, 26, 12}},
    {"EQ_ON_BUTTON_DEPRESSED", {128, 119, 26, 12}},
    {"EQ_ON_BUTTON_SELECTED", {69, 119, 26, 12}},
    {"EQ_ON_BUTTON_SELECTED_DEPRESSED", {187, 119, 26, 12}},
    {"EQ_AUTO_BUTTON", {36, 119, 32, 12}},
    {"EQ_AUTO_BUTTON_DEPRESSED", {154, 119, 32, 12}},
    {"EQ_AUTO_BUTTON_SELECTED", {95, 119, 32, 12}},
    {"EQ_AUTO_BUTTON_SELECTED_DEPRESSED", {213, 119, 32, 12}},
    {"EQ_GRAPH_BACKGROUND", {0, 294, 113, 19}},
    {"EQ_GRAPH_LINE_COLORS", {115, 294, 1, 19}},
    {"EQ_PRESETS_BUTTON", {224, 164, 44, 12}},
    {"EQ_PRESETS_BUTTON_SELECTED", {224, 176, 44, 12}},
    {"EQ_PREAMP_LINE", {0, 314, 113, 1}},
}};

// --- PLEDIT.BMP ---
inline constexpr std::array<SpriteDef, 60> kSpritesPLEDIT = {{
    {"PLAYLIST_TOP_TILE", {127, 21, 25, 20}},
    {"PLAYLIST_TOP_LEFT_CORNER", {0, 21, 25, 20}},
    {"PLAYLIST_TITLE_BAR", {26, 21, 100, 20}},
    {"PLAYLIST_TOP_RIGHT_CORNER", {153, 21, 25, 20}},
    {"PLAYLIST_TOP_TILE_SELECTED", {127, 0, 25, 20}},
    {"PLAYLIST_TOP_LEFT_SELECTED", {0, 0, 25, 20}},
    {"PLAYLIST_TITLE_BAR_SELECTED", {26, 0, 100, 20}},
    {"PLAYLIST_TOP_RIGHT_CORNER_SELECTED", {153, 0, 25, 20}},
    {"PLAYLIST_LEFT_TILE", {0, 42, 12, 29}},
    {"PLAYLIST_RIGHT_TILE", {31, 42, 20, 29}},
    {"PLAYLIST_BOTTOM_TILE", {179, 0, 25, 38}},
    {"PLAYLIST_BOTTOM_LEFT_CORNER", {0, 72, 125, 38}},
    {"PLAYLIST_BOTTOM_RIGHT_CORNER", {126, 72, 150, 38}},
    {"PLAYLIST_VISUALIZER_BACKGROUND", {205, 0, 75, 38}},
    {"PLAYLIST_SHADE_BACKGROUND", {72, 57, 25, 14}},
    {"PLAYLIST_SHADE_BACKGROUND_LEFT", {72, 42, 25, 14}},
    {"PLAYLIST_SHADE_BACKGROUND_RIGHT", {99, 57, 50, 14}},
    {"PLAYLIST_SHADE_BACKGROUND_RIGHT_SELECTED", {99, 42, 50, 14}},
    {"PLAYLIST_SCROLL_HANDLE_SELECTED", {61, 53, 8, 18}},
    {"PLAYLIST_SCROLL_HANDLE", {52, 53, 8, 18}},
    {"PLAYLIST_ADD_URL", {0, 111, 22, 18}},
    {"PLAYLIST_ADD_URL_SELECTED", {23, 111, 22, 18}},
    {"PLAYLIST_ADD_DIR", {0, 130, 22, 18}},
    {"PLAYLIST_ADD_DIR_SELECTED", {23, 130, 22, 18}},
    {"PLAYLIST_ADD_FILE", {0, 149, 22, 18}},
    {"PLAYLIST_ADD_FILE_SELECTED", {23, 149, 22, 18}},
    {"PLAYLIST_REMOVE_ALL", {54, 111, 22, 18}},
    {"PLAYLIST_REMOVE_ALL_SELECTED", {77, 111, 22, 18}},
    {"PLAYLIST_CROP", {54, 130, 22, 18}},
    {"PLAYLIST_CROP_SELECTED", {77, 130, 22, 18}},
    {"PLAYLIST_REMOVE_SELECTED", {54, 149, 22, 18}},
    {"PLAYLIST_REMOVE_SELECTED_SELECTED", {77, 149, 22, 18}},
    {"PLAYLIST_REMOVE_MISC", {54, 168, 22, 18}},
    {"PLAYLIST_REMOVE_MISC_SELECTED", {77, 168, 22, 18}},
    {"PLAYLIST_INVERT_SELECTION", {104, 111, 22, 18}},
    {"PLAYLIST_INVERT_SELECTION_SELECTED", {127, 111, 22, 18}},
    {"PLAYLIST_SELECT_ZERO", {104, 130, 22, 18}},
    {"PLAYLIST_SELECT_ZERO_SELECTED", {127, 130, 22, 18}},
    {"PLAYLIST_SELECT_ALL", {104, 149, 22, 18}},
    {"PLAYLIST_SELECT_ALL_SELECTED", {127, 149, 22, 18}},
    {"PLAYLIST_SORT_LIST", {154, 111, 22, 18}},
    {"PLAYLIST_SORT_LIST_SELECTED", {177, 111, 22, 18}},
    {"PLAYLIST_FILE_INFO", {154, 130, 22, 18}},
    {"PLAYLIST_FILE_INFO_SELECTED", {177, 130, 22, 18}},
    {"PLAYLIST_MISC_OPTIONS", {154, 149, 22, 18}},
    {"PLAYLIST_MISC_OPTIONS_SELECTED", {177, 149, 22, 18}},
    {"PLAYLIST_NEW_LIST", {204, 111, 22, 18}},
    {"PLAYLIST_NEW_LIST_SELECTED", {227, 111, 22, 18}},
    {"PLAYLIST_SAVE_LIST", {204, 130, 22, 18}},
    {"PLAYLIST_SAVE_LIST_SELECTED", {227, 130, 22, 18}},
    {"PLAYLIST_LOAD_LIST", {204, 149, 22, 18}},
    {"PLAYLIST_LOAD_LIST_SELECTED", {227, 149, 22, 18}},
    {"PLAYLIST_ADD_MENU_BAR", {48, 111, 3, 54}},
    {"PLAYLIST_REMOVE_MENU_BAR", {100, 111, 3, 72}},
    {"PLAYLIST_SELECT_MENU_BAR", {150, 111, 3, 54}},
    {"PLAYLIST_MISC_MENU_BAR", {200, 111, 3, 54}},
    {"PLAYLIST_LIST_BAR", {250, 111, 3, 54}},
    {"PLAYLIST_CLOSE_SELECTED", {52, 42, 9, 9}},
    {"PLAYLIST_COLLAPSE_SELECTED", {62, 42, 9, 9}},
    {"PLAYLIST_EXPAND_SELECTED", {150, 42, 9, 9}},
}};

// --- GEN.BMP ---
inline constexpr std::array<SpriteDef, 20> kSpritesGEN = {{
    {"GEN_TOP_LEFT_SELECTED", {0, 0, 25, 20}},
    {"GEN_TOP_LEFT_END_SELECTED", {26, 0, 25, 20}},
    {"GEN_TOP_CENTER_FILL_SELECTED", {52, 0, 25, 20}},
    {"GEN_TOP_RIGHT_END_SELECTED", {78, 0, 25, 20}},
    {"GEN_TOP_LEFT_RIGHT_FILL_SELECTED", {104, 0, 25, 20}},
    {"GEN_TOP_RIGHT_SELECTED", {130, 0, 25, 20}},
    {"GEN_TOP_LEFT", {0, 21, 25, 20}},
    {"GEN_TOP_LEFT_END", {26, 21, 25, 20}},
    {"GEN_TOP_CENTER_FILL", {52, 21, 25, 20}},
    {"GEN_TOP_RIGHT_END", {78, 21, 25, 20}},
    {"GEN_TOP_LEFT_RIGHT_FILL", {104, 21, 25, 20}},
    {"GEN_TOP_RIGHT", {130, 21, 25, 20}},
    {"GEN_BOTTOM_LEFT", {0, 42, 125, 14}},
    {"GEN_BOTTOM_RIGHT", {0, 57, 125, 14}},
    {"GEN_BOTTOM_FILL", {127, 72, 25, 14}},
    {"GEN_MIDDLE_LEFT", {127, 42, 11, 29}},
    {"GEN_MIDDLE_LEFT_BOTTOM", {158, 42, 11, 24}},
    {"GEN_MIDDLE_RIGHT", {139, 42, 8, 29}},
    {"GEN_MIDDLE_RIGHT_BOTTOM", {170, 42, 8, 24}},
    {"GEN_CLOSE_SELECTED", {148, 42, 9, 9}},
}};

}  // namespace fe3
