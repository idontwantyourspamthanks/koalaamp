// SPDX-License-Identifier: MIT
#include "MainWindowRenderer.hpp"

#include <cmath>

#include <QPainter>

namespace fe3 {

using koalaamp::skins::fontGlyph;   // promoted sprite table (src/skins/SkinSprites.hpp)
using koalaamp::skins::kCharWidth;

namespace {

// --- Layout constants (ported from Webamp css/main-window.css, MIT) ---------
struct Rect {
    int x, y, w, h;
};

constexpr Rect kTitleBar{0, 0, 275, 14};
constexpr Rect kClutterBar{10, 22, 8, 43};
constexpr Rect kPlayPause{26, 28, 9, 9};
constexpr Rect kWorkIndicator{24, 28, 3, 9};
constexpr Rect kMarquee{111, 24, 154, 12};
constexpr Rect kMono{212, 41, 27, 12};
constexpr Rect kStereo{239, 41, 29, 12};
constexpr Rect kVolume{107, 57, 68, 13};
constexpr Rect kBalance{177, 57, 38, 13};
constexpr Rect kEqButton{219, 58, 23, 12};
constexpr Rect kPlButton{242, 58, 23, 12};
constexpr Rect kPosition{16, 72, 248, 10};
constexpr Rect kPrevButton{16, 88, 23, 18};
constexpr Rect kPlayButton{39, 88, 23, 18};
constexpr Rect kPauseButton{62, 88, 23, 18};
constexpr Rect kStopButton{85, 88, 23, 18};
constexpr Rect kNextButton{108, 88, 22, 18};  // Webamp clips the 23px sprite to 22
constexpr Rect kEjectButton{136, 89, 22, 16};
constexpr Rect kShuffleButton{164, 89, 47, 15};
constexpr Rect kRepeatButton{210, 89, 28, 15};

// Slider tracks and thumb geometry (Webamp: input width - thumb width).
constexpr int kThumbW = 14;
constexpr int kThumbH = 11;
constexpr int kVolumeTrackW = 65;   // #volume input width
constexpr int kBalanceTrackW = 38;  // #balance input width
constexpr int kBalanceSheetX = 9;   // balance.bmp groove starts at x=9

// Webamp: sprite = round(percent * 28); offset = (sprite - 1) * 15.
int volumeFrameOffset(int volume) {
    const int sprite = static_cast<int>(std::lround((volume / 100.0) * 28.0));
    return (sprite - 1) * 15;
}

// Webamp: sprite = floor(|balance| / 100 * 27); offset = sprite * 15.
int balanceFrameOffset(int balance) {
    const int sprite = static_cast<int>(std::floor((std::abs(balance) / 100.0) * 27.0));
    return sprite * 15;
}

int thumbLeft(int value, int minValue, int maxValue, int trackW) {
    const double fraction =
        static_cast<double>(value - minValue) / static_cast<double>(maxValue - minValue);
    return static_cast<int>(std::lround(fraction * (trackW - kThumbW)));
}

void drawDigits(const koalaamp::skins::Skin &skin, QPainter &painter, int seconds) {
    // Time layout: #time at (39,26); digit lefts 9/21/39/51 (Webamp CSS).
    static constexpr int kDigitX[4] = {39 + 9, 39 + 21, 39 + 39, 39 + 51};
    const int minutes = seconds / 60;
    const int secs = seconds % 60;
    const int digits[4] = {minutes / 10 % 10, minutes % 10, secs / 10 % 10, secs % 10};
    for (int i = 0; i < 4; ++i) {
        const std::string name = "DIGIT_" + std::to_string(digits[i]);
        skin.blit(painter, "NUMBERS", name, kDigitX[i], 26);
    }
}

void drawMarquee(const koalaamp::skins::Skin &skin, QPainter &painter, const std::string &text) {
    int x = kMarquee.x;
    for (unsigned char c : text) {
        const auto glyph = fontGlyph(static_cast<char32_t>(c));
        if (glyph) {
            const auto *sheet = skin.sheet("TEXT");
            if (sheet) {
                painter.drawImage(QPoint(x, 27), *sheet, QRect(glyph->x, glyph->y, glyph->w, glyph->h));
            }
        }
        x += kCharWidth;
        if (x >= kMarquee.x + kMarquee.w) {
            break;
        }
    }
}

}  // namespace

QImage renderMainWindow(const koalaamp::skins::Skin &skin, const MainWindowState &state) {
    QImage canvas(275, 116, QImage::Format_RGB32);
    canvas.fill(Qt::black);

    QPainter painter(&canvas);
    // No smoothing anywhere: exact integer blits (spec §5.2).
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    // Background first.
    skin.blit(painter, "MAIN", "MAIN_WINDOW_BACKGROUND", 0, 0);

    // Title bar. Its 275x14 art already carries the menu/min/shade/close icons
    // for base-2.91: Webamp only overlays the standalone button sprites in their
    // *clicked/active* state (skinSelectors.ts), so idle draws none.
    skin.blit(painter, "TITLEBAR", "MAIN_TITLE_BAR_SELECTED", kTitleBar.x, kTitleBar.y);

    // Clutter bar (letters are baked into the background sprite).
    skin.blit(painter, "TITLEBAR", "MAIN_CLUTTER_BAR_BACKGROUND", kClutterBar.x, kClutterBar.y);

    // Play/pause/stop indicator + work indicator.
    const char *indicator = "MAIN_STOPPED_INDICATOR";
    if (state.status == MediaStatus::Playing) {
        indicator = "MAIN_PLAYING_INDICATOR";
    } else if (state.status == MediaStatus::Paused) {
        indicator = "MAIN_PAUSED_INDICATOR";
    }
    skin.blit(painter, "PLAYPAUS", indicator, kPlayPause.x, kPlayPause.y);
    if (state.working || state.status == MediaStatus::Playing) {
        skin.blit(painter, "PLAYPAUS", "MAIN_WORKING_INDICATOR", kWorkIndicator.x,
                  kWorkIndicator.y, kWorkIndicator.w);
    }

    if (state.status != MediaStatus::Stopped && state.timeSeconds >= 0) {
        drawDigits(skin, painter, state.timeSeconds);
    }

    drawMarquee(skin, painter, state.marquee);

    // Mono/stereo indicators (overlapping sprites, last-drawn wins).
    skin.blit(painter, "MONOSTER",
              state.channels == 2 ? "MAIN_STEREO_SELECTED" : "MAIN_STEREO", kStereo.x, kStereo.y);
    skin.blit(painter, "MONOSTER", state.channels == 1 ? "MAIN_MONO_SELECTED" : "MAIN_MONO",
              kMono.x, kMono.y);

    // Volume + thumb.
    const int volOffset = volumeFrameOffset(state.volume);
    skin.blitRect(painter, "VOLUME", QRect(0, volOffset, kVolume.w, kVolume.h), kVolume.x,
                  kVolume.y);
    const int volThumbLeft = thumbLeft(state.volume, 0, 100, kVolumeTrackW);
    skin.blit(painter, "VOLUME", "MAIN_VOLUME_THUMB", kVolume.x + volThumbLeft,
              kVolume.y + 1);

    // Balance + thumb (groove starts at x=9 in balance.bmp — the off-by-9 trap).
    const int balOffset = balanceFrameOffset(state.balance);
    skin.blitRect(painter, "BALANCE",
                  QRect(kBalanceSheetX, balOffset, kBalance.w, kBalance.h), kBalance.x,
                  kBalance.y);
    const int balThumbLeft = thumbLeft(state.balance, -100, 100, kBalanceTrackW);
    skin.blit(painter, "BALANCE", "MAIN_BALANCE_THUMB", kBalance.x + balThumbLeft,
              kBalance.y + 1);

    // EQ / PL toggle buttons.
    skin.blit(painter, "SHUFREP",
              state.eqSelected ? "MAIN_EQ_BUTTON_SELECTED" : "MAIN_EQ_BUTTON", kEqButton.x,
              kEqButton.y);
    skin.blit(painter, "SHUFREP",
              state.plSelected ? "MAIN_PLAYLIST_BUTTON_SELECTED" : "MAIN_PLAYLIST_BUTTON",
              kPlButton.x, kPlButton.y);

    // Seek bar (thumb only while not stopped).
    skin.blit(painter, "POSBAR", "MAIN_POSITION_SLIDER_BACKGROUND", kPosition.x, kPosition.y);
    if (state.status != MediaStatus::Stopped) {
        skin.blit(painter, "POSBAR", "MAIN_POSITION_SLIDER_THUMB", kPosition.x, kPosition.y);
    }

    // Transport buttons.
    const bool prevActive = state.activeButton == "previous";
    const bool playActive = state.activeButton == "play";
    const bool pauseActive = state.activeButton == "pause";
    const bool stopActive = state.activeButton == "stop";
    const bool nextActive = state.activeButton == "next";
    const bool ejectActive = state.activeButton == "eject";
    skin.blit(painter, "CBUTTONS", prevActive ? "MAIN_PREVIOUS_BUTTON_ACTIVE" : "MAIN_PREVIOUS_BUTTON",
              kPrevButton.x, kPrevButton.y);
    skin.blit(painter, "CBUTTONS", playActive ? "MAIN_PLAY_BUTTON_ACTIVE" : "MAIN_PLAY_BUTTON",
              kPlayButton.x, kPlayButton.y);
    skin.blit(painter, "CBUTTONS", pauseActive ? "MAIN_PAUSE_BUTTON_ACTIVE" : "MAIN_PAUSE_BUTTON",
              kPauseButton.x, kPauseButton.y);
    skin.blit(painter, "CBUTTONS", stopActive ? "MAIN_STOP_BUTTON_ACTIVE" : "MAIN_STOP_BUTTON",
              kStopButton.x, kStopButton.y);
    skin.blit(painter, "CBUTTONS", nextActive ? "MAIN_NEXT_BUTTON_ACTIVE" : "MAIN_NEXT_BUTTON",
              kNextButton.x, kNextButton.y, kNextButton.w);
    skin.blit(painter, "CBUTTONS", ejectActive ? "MAIN_EJECT_BUTTON_ACTIVE" : "MAIN_EJECT_BUTTON",
              kEjectButton.x, kEjectButton.y);

    // Shuffle / repeat.
    skin.blit(painter, "SHUFREP",
              state.shuffleSelected ? "MAIN_SHUFFLE_BUTTON_SELECTED" : "MAIN_SHUFFLE_BUTTON",
              kShuffleButton.x, kShuffleButton.y);
    skin.blit(painter, "SHUFREP",
              state.repeatSelected ? "MAIN_REPEAT_BUTTON_SELECTED" : "MAIN_REPEAT_BUTTON",
              kRepeatButton.x, kRepeatButton.y);

    painter.end();
    return canvas;
}

}  // namespace fe3
