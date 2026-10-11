// SPDX-License-Identifier: MIT
#include "ui/PlayerRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <QPainter>
#include <QRect>

#include "skins/SkinSprites.hpp"
#include "ui/TitleText.hpp"

namespace koalaamp::ui {

using koalaamp::skins::fontGlyph;
using koalaamp::skins::Skin;
using koalaamp::skins::SpriteRect;
using koalaamp::skins::VisColor;

namespace {

/// Source columns of the 3 px work-indicator slice in `playpaus.bmp` (Webamp:
/// `background-position -36px/-39px` on a 3 px element).
constexpr int kWorkIndicatorIdleX = 36;
constexpr int kWorkIndicatorWorkingX = 39;

/// An RGB(8-bit) viscolor entry as a QColor.
QColor visColorAt(const VisColor& vis, std::size_t index) {
    if (index >= vis.colors.size()) {
        return QColor(0, 0, 0);
    }
    const auto& c = vis.colors[index];
    return QColor(c.r, c.g, c.b);
}

/// One character of the 5x6 `text.bmp` font, opaque (background included).
/// A codepoint with no glyph leaves the destination untouched but still
/// advances the pen by one cell.
void drawGlyph(const Skin& skin, QPainter& painter, char32_t cp, int x, int y) {
    const auto glyph = fontGlyph(cp);
    if (!glyph) {
        return;
    }
    const auto* sheet = skin.sheet("TEXT");
    if (!sheet) {
        return;
    }
    painter.drawImage(QPoint(x, y), *sheet, QRect(glyph->x, glyph->y, glyph->w, glyph->h));
}

void drawText(const Skin& skin, QPainter& painter, const std::string& text, int x, int y,
              int clipWidth) {
    const bool clipped = clipWidth > 0;
    if (clipped) {
        painter.save();
        painter.setClipRect(QRect(x, y, clipWidth, koalaamp::skins::kCharHeight));
    }
    int penX = x;
    for (std::size_t i = 0; i < text.size();) {
        const char32_t cp = nextCodepoint(text, i);
        drawGlyph(skin, painter, foldForFont(cp), penX, y);
        penX += koalaamp::skins::kCharWidth;
    }
    if (clipped) {
        painter.restore();
    }
}

/// The `#time` digits. With `nums_ex.bmp` present the skin's digits are the
/// `_EX` sprites (Webamp augments its CSS with NUM_EX.BMP when present).
void drawTime(const Skin& skin, QPainter& painter, const PlayerState& state) {
    const int elapsed = std::max(0, state.timeSeconds);
    const int minutes = elapsed / 60;
    const int seconds = elapsed % 60;
    const int digits[4] = {minutes / 10 % 10, minutes % 10, seconds / 10 % 10, seconds % 10};

    const bool useEx = skin.sheet("NUMS_EX") != nullptr;
    const char* sheetName = useEx ? "NUMS_EX" : "NUMBERS";
    for (int i = 0; i < 4; ++i) {
        const std::string name =
            (useEx ? "DIGIT_" : "DIGIT_") + std::to_string(digits[i]) + (useEx ? "_EX" : "");
        skin.blit(painter, sheetName, name, ShadeLayout::digitX[i], ShadeLayout::digitY);
    }

    // The minus sign only exists in countdown mode (Webamp renders the
    // `#minus-sign` element only for REMAINING). `numbers.bmp` borrows a 5x1
    // slice of digit 2's midline; `nums_ex.bmp` carries a full 9x13 glyph.
    if (state.countdown) {
        if (useEx) {
            skin.blit(painter, "NUMS_EX", "MINUS_SIGN_EX", ShadeLayout::minusSignEx.x(),
                      ShadeLayout::minusSignEx.y());
        } else {
            skin.blit(painter, "NUMBERS", "MINUS_SIGN", ShadeLayout::minusSign.x(),
                      ShadeLayout::minusSign.y());
        }
    }
}

/// The in-window visualiser: bg + dither dots, then the bars/peaks or the
/// oscilloscope. Faithful to Webamp's `js/visualizer/*` (MIT): a 76x16 canvas
/// filled with viscolor[0], a dot grid in viscolor[1] at even x / odd y, 75
/// one-pixel bars drawn from the 16-row gradient strip, peaks in viscolor[23].
/// `drawColumns` false paints only the background layer, which is what the
/// paint path caches (the columns are redrawn every animation tick).
void drawVisArea(QPainter& painter, const VisColor& vis, const PlayerState& state, int scale,
                 bool drawColumns) {
    // The vis canvas exists only once a track is loaded (Webamp renders no
    // canvas while stopped, so main.bmp's art shows through) and it keeps the
    // last painted frame while paused.
    if (state.status == PlayerStatus::Stopped || state.vis == VisMode::Off) {
        return;
    }
    const QRect area = MainLayout::visualizer;
    painter.save();
    painter.setClipRect(QRect(area.x() * scale, area.y() * scale, area.width() * scale,
                              area.height() * scale));

    const int pixel = scale;
    painter.fillRect(QRect(area.x() * scale, area.y() * scale, area.width() * scale,
                           area.height() * scale),
                     visColorAt(vis, 0));
    for (int x = 0; x < area.width(); x += 2) {
        for (int y = 1; y < area.height(); y += 2) {
            painter.fillRect(area.x() * scale + x * pixel, area.y() * scale + y * pixel, pixel,
                             pixel, visColorAt(vis, 1));
        }
    }
    if (!drawColumns) {
        painter.restore();
        return;
    }

    // Webamp's 1x main window draws with pushDown 2.
    constexpr int kPushDown = 2;
    if (state.vis == VisMode::Spectrum) {
        if (state.visFrame.barsValid) {
            for (int i = 0; i < kVisBars; ++i) {
                const int height = static_cast<int>(state.visFrame.bars[i]) - kPushDown;
                const int top = std::max(0, area.height() - height);
                for (int row = top; row < area.height(); ++row) {
                    painter.fillRect(area.x() * scale + i * pixel, area.y() * scale + row * pixel,
                                     pixel, pixel, visColorAt(vis, static_cast<std::size_t>(row)));
                }
                if (state.visFrame.peaks[i] != kPeakOff) {
                    const int peak = static_cast<int>(state.visFrame.peaks[i]) + 1 - kPushDown;
                    const int y = area.height() - peak;
                    if (y >= 0 && y < area.height()) {
                        painter.fillRect(area.x() * scale + i * pixel, area.y() * scale + y * pixel,
                                         pixel, pixel, visColorAt(vis, 23));
                    }
                }
            }
        }
    } else if (state.visFrame.oscValid) {
        // Oscilloscope: one column per point, drawn from the previous point's
        // row to this one, coloured by the point's own row (Webamp
        // `VC::paintOscilloscope`; `colorIndex` maps a row to viscolor 18..22).
        const auto colorIndex = [](int y) {
            if (y >= 14) return 4;
            if (y >= 12) return 3;
            if (y >= 10) return 2;
            if (y >= 8) return 1;
            if (y >= 6) return 0;
            if (y >= 4) return 1;
            if (y >= 2) return 2;
            return 3;
        };
        int previous = state.visFrame.osc[0];
        for (int i = 0; i < kVisBars; ++i) {
            const int current = state.visFrame.osc[i];
            const int low = std::min(previous, current);
            const int high = std::max(previous, current);
            for (int row = low; row <= high; ++row) {
                const int y = row + kPushDown;
                if (y >= 0 && y < area.height()) {
                    painter.fillRect(
                        area.x() * scale + i * pixel, area.y() * scale + y * pixel, pixel, pixel,
                        visColorAt(vis, 18 + static_cast<std::size_t>(colorIndex(current))));
                }
            }
            previous = current;
        }
    }
    painter.restore();
}

void drawMainWindowLayout(const Skin& skin, QPainter& painter, const VisColor& vis,
                          const PlayerState& state) {
    painter.drawImage(QPoint(0, 0), *skin.sheet("MAIN"), QRect(0, 0, kMainWidth, kMainHeight));

    // --- title bar ---------------------------------------------------------
    const char* titleSprite = state.llama ? "MAIN_EASTER_EGG_TITLE_BAR" : "MAIN_TITLE_BAR";
    if (state.selected) {
        titleSprite = state.llama ? "MAIN_EASTER_EGG_TITLE_BAR_SELECTED" : "MAIN_TITLE_BAR_SELECTED";
    }
    skin.blit(painter, "TITLEBAR", titleSprite, MainLayout::titleBar.x(), MainLayout::titleBar.y());

    // The 275x14 title-bar art already carries the idle button glyphs; Webamp
    // only overlays the standalone sprites while a button is held/clicked.
    if (state.pressed.option) {
        skin.blit(painter, "TITLEBAR", "MAIN_OPTIONS_BUTTON_DEPRESSED", MainLayout::option.x(),
                  MainLayout::option.y());
    }
    if (state.pressed.minimize) {
        skin.blit(painter, "TITLEBAR", "MAIN_MINIMIZE_BUTTON_DEPRESSED",
                  MainLayout::minimize.x(), MainLayout::minimize.y());
    }
    if (state.pressed.close) {
        skin.blit(painter, "TITLEBAR", "MAIN_CLOSE_BUTTON_DEPRESSED", MainLayout::close.x(),
                  MainLayout::close.y());
    }

    // --- clutter bar -------------------------------------------------------
    skin.blit(painter, "TITLEBAR",
              state.clutter.enabled ? "MAIN_CLUTTER_BAR_BACKGROUND"
                                    : "MAIN_CLUTTER_BAR_BACKGROUND_DISABLED",
              MainLayout::clutterBar.x(), MainLayout::clutterBar.y());
    const auto clutterBlit = [&](bool selected, const char* sprite, const QRect& rel) {
        if (selected) {
            skin.blit(painter, "TITLEBAR", sprite, MainLayout::clutterBar.x() + rel.x(),
                      MainLayout::clutterBar.y() + rel.y());
        }
    };
    clutterBlit(state.clutter.o, "MAIN_CLUTTER_BAR_BUTTON_O_SELECTED", MainLayout::clutterO);
    clutterBlit(state.clutter.a, "MAIN_CLUTTER_BAR_BUTTON_A_SELECTED", MainLayout::clutterA);
    clutterBlit(state.clutter.i, "MAIN_CLUTTER_BAR_BUTTON_I_SELECTED", MainLayout::clutterI);
    clutterBlit(state.clutter.d, "MAIN_CLUTTER_BAR_BUTTON_D_SELECTED", MainLayout::clutterD);
    clutterBlit(state.clutter.v, "MAIN_CLUTTER_BAR_BUTTON_V_SELECTED", MainLayout::clutterV);

    // --- play/pause/stop indicator + work indicator -------------------------
    const char* indicator = "MAIN_STOPPED_INDICATOR";
    if (state.status == PlayerStatus::Playing) {
        indicator = "MAIN_PLAYING_INDICATOR";
    } else if (state.status == PlayerStatus::Paused) {
        indicator = "MAIN_PAUSED_INDICATOR";
    }
    skin.blit(painter, "PLAYPAUS", indicator, MainLayout::playPause.x(), MainLayout::playPause.y());
    if (state.working || state.status == PlayerStatus::Playing) {
        // The work indicator is the 3 px sliver left of the play glyph. The
        // sprite table records `MAIN_{NOT_,}WORKING_INDICATOR` as 9x9 rects at
        // x=36/39, but `playpaus.bmp` is only 42 px wide and Webamp's element
        // is 3 px — so the *used* source is the 3 px slice.
        const int sourceX = state.working ? kWorkIndicatorWorkingX : kWorkIndicatorIdleX;
        skin.blitRect(painter, "PLAYPAUS",
                      QRect(sourceX, 0, MainLayout::workIndicator.width(),
                            MainLayout::workIndicator.height()),
                      MainLayout::workIndicator.x(), MainLayout::workIndicator.y());
    }

    // --- visualiser --------------------------------------------------------
    drawVisArea(painter, vis, state, 1, true);

    // --- media info --------------------------------------------------------
    const bool hasTrack = state.status != PlayerStatus::Stopped;
    if (hasTrack) {
        const std::string kbps =
            state.kbpsKnown ? formatKbps(state.kbps) : std::string("  0");
        const std::string khz = state.khzKnown ? formatKhz(state.khz) : std::string(" 0");
        drawText(skin, painter, kbps, MainLayout::kbps.x(), MainLayout::kbps.y(),
                 MainLayout::kbps.width());
        drawText(skin, painter, khz, MainLayout::khz.x(), MainLayout::khz.y(),
                 MainLayout::khz.width());
    }
    if (state.timeVisible && hasTrack && state.timeBlinkOn) {
        drawTime(skin, painter, state);
    }

    // --- title marquee -----------------------------------------------------
    // `#marquee` is a 154 px viewport with `overflow: hidden`, so both edges
    // clip: a scrolling title's leading cells must not paint outside it.
    painter.save();
    painter.setClipRect(MainLayout::marquee.x(), MainLayout::marquee.y() + 3,
                        MainLayout::marquee.width(), koalaamp::skins::kCharHeight);
    drawText(skin, painter, marqueeDisplay(state.title),
             MainLayout::marquee.x() - state.marqueeOffsetPx, MainLayout::marquee.y() + 3, 0);
    painter.restore();

    // --- mono / stereo -----------------------------------------------------
    const bool monoSelected = state.channels == 1 && hasTrack;
    const bool stereoSelected = state.channels == 2 && hasTrack;
    skin.blit(painter, "MONOSTER", stereoSelected ? "MAIN_STEREO_SELECTED" : "MAIN_STEREO",
              MainLayout::stereo.x(), MainLayout::stereo.y());
    skin.blit(painter, "MONOSTER", monoSelected ? "MAIN_MONO_SELECTED" : "MAIN_MONO",
              MainLayout::mono.x(), MainLayout::mono.y());

    // --- volume + balance --------------------------------------------------
    const int volFrame = volumeFrameOffset(state.volume);
    skin.blitRect(painter, "VOLUME", QRect(0, volFrame, MainLayout::volume.width(), 13),
                  MainLayout::volume.x(), MainLayout::volume.y());
    skin.blit(painter, "VOLUME",
              state.volumeActive ? "MAIN_VOLUME_THUMB_SELECTED" : "MAIN_VOLUME_THUMB",
              MainLayout::volume.x() +
                  thumbLeft(state.volume, 0, 100, SliderMetrics::volumeTrackW,
                            SliderMetrics::thumbW),
              MainLayout::volume.y() + 1);

    const int balFrame = balanceFrameOffset(state.balance);
    skin.blitRect(painter, "BALANCE",
                  QRect(SliderMetrics::balanceSheetX, balFrame, MainLayout::balance.width(), 13),
                  MainLayout::balance.x(), MainLayout::balance.y());
    skin.blit(painter, "BALANCE",
              state.balanceActive ? "MAIN_BALANCE_THUMB_ACTIVE" : "MAIN_BALANCE_THUMB",
              MainLayout::balance.x() +
                  thumbLeft(state.balance, -100, 100, SliderMetrics::balanceTrackW,
                            SliderMetrics::thumbW),
              MainLayout::balance.y() + 1);

    // --- EQ / playlist toggles --------------------------------------------
    const char* eqSprite = state.eqSelected ? "MAIN_EQ_BUTTON_SELECTED" : "MAIN_EQ_BUTTON";
    if (state.eqDepressed) {
        eqSprite = state.eqSelected ? "MAIN_EQ_BUTTON_DEPRESSED_SELECTED" : "MAIN_EQ_BUTTON_DEPRESSED";
    }
    skin.blit(painter, "SHUFREP", eqSprite, MainLayout::eqButton.x(), MainLayout::eqButton.y());
    const char* plSprite = state.plSelected ? "MAIN_PLAYLIST_BUTTON_SELECTED"
                                            : "MAIN_PLAYLIST_BUTTON";
    if (state.plDepressed) {
        plSprite = state.plSelected ? "MAIN_PLAYLIST_BUTTON_DEPRESSED_SELECTED"
                                    : "MAIN_PLAYLIST_BUTTON_DEPRESSED";
    }
    skin.blit(painter, "SHUFREP", plSprite, MainLayout::plButton.x(), MainLayout::plButton.y());

    // --- seek bar ----------------------------------------------------------
    skin.blit(painter, "POSBAR", "MAIN_POSITION_SLIDER_BACKGROUND", MainLayout::position.x(),
              MainLayout::position.y());
    if (hasTrack) {
        skin.blit(painter, "POSBAR",
                  state.positionActive ? "MAIN_POSITION_SLIDER_THUMB_SELECTED"
                                       : "MAIN_POSITION_SLIDER_THUMB",
                  MainLayout::position.x() +
                      thumbLeft(state.position, 0, 100, MainLayout::position.width(),
                                SliderMetrics::positionThumbW),
                  MainLayout::position.y());
    }

    // --- transport ---------------------------------------------------------
    const auto buttonSprite = [](const char* idle, const char* active, bool down) {
        return down ? active : idle;
    };
    skin.blit(painter, "CBUTTONS",
              buttonSprite("MAIN_PREVIOUS_BUTTON", "MAIN_PREVIOUS_BUTTON_ACTIVE",
                           state.active.previous),
              MainLayout::previous.x(), MainLayout::previous.y());
    skin.blit(painter, "CBUTTONS",
              buttonSprite("MAIN_PLAY_BUTTON", "MAIN_PLAY_BUTTON_ACTIVE", state.active.play),
              MainLayout::play.x(), MainLayout::play.y());
    skin.blit(painter, "CBUTTONS",
              buttonSprite("MAIN_PAUSE_BUTTON", "MAIN_PAUSE_BUTTON_ACTIVE", state.active.pause),
              MainLayout::pause.x(), MainLayout::pause.y());
    skin.blit(painter, "CBUTTONS",
              buttonSprite("MAIN_STOP_BUTTON", "MAIN_STOP_BUTTON_ACTIVE", state.active.stop),
              MainLayout::stop.x(), MainLayout::stop.y());
    skin.blit(painter, "CBUTTONS",
              buttonSprite("MAIN_NEXT_BUTTON", "MAIN_NEXT_BUTTON_ACTIVE", state.active.next),
              MainLayout::next.x(), MainLayout::next.y(), MainLayout::next.width());
    skin.blit(painter, "CBUTTONS",
              buttonSprite("MAIN_EJECT_BUTTON", "MAIN_EJECT_BUTTON_ACTIVE", state.active.eject),
              MainLayout::eject.x(), MainLayout::eject.y());

    // --- shuffle / repeat (four states each, 1 px overlap) ------------------
    const char* shuffleSprite = state.shuffle ? "MAIN_SHUFFLE_BUTTON_SELECTED"
                                              : "MAIN_SHUFFLE_BUTTON";
    if (state.shuffleDepressed) {
        shuffleSprite = state.shuffle ? "MAIN_SHUFFLE_BUTTON_SELECTED_DEPRESSED"
                                      : "MAIN_SHUFFLE_BUTTON_DEPRESSED";
    }
    const char* repeatSprite = state.repeat ? "MAIN_REPEAT_BUTTON_SELECTED"
                                           : "MAIN_REPEAT_BUTTON";
    if (state.repeatDepressed) {
        repeatSprite = state.repeat ? "MAIN_REPEAT_BUTTON_SELECTED_DEPRESSED"
                                    : "MAIN_REPEAT_BUTTON_DEPRESSED";
    }
    skin.blit(painter, "SHUFREP", shuffleSprite, MainLayout::shuffle.x(),
              MainLayout::shuffle.y());
    skin.blit(painter, "SHUFREP", repeatSprite, MainLayout::repeat.x(),
              MainLayout::repeat.y());
}

void drawShadeWindow(const Skin& skin, QPainter& painter, const PlayerState& state) {
    skin.blit(painter, "TITLEBAR",
              state.selected ? "MAIN_SHADE_BACKGROUND_SELECTED" : "MAIN_SHADE_BACKGROUND",
              ShadeLayout::background.x(), ShadeLayout::background.y());

    // The shade button is always composited in shade mode while the window is
    // selected (Webamp `.shade.selected #title-bar #shade`); its neighbours are
    // only drawn while held down.
    if (state.selected) {
        skin.blit(painter, "TITLEBAR",
                  state.pressed.shade ? "MAIN_SHADE_BUTTON_SELECTED_DEPRESSED"
                                      : "MAIN_SHADE_BUTTON_SELECTED",
                  ShadeLayout::shadeButton.x(), ShadeLayout::shadeButton.y());
    }
    if (state.pressed.option) {
        skin.blit(painter, "TITLEBAR", "MAIN_OPTIONS_BUTTON_DEPRESSED", MainLayout::option.x(),
                  MainLayout::option.y());
    }
    if (state.pressed.minimize) {
        skin.blit(painter, "TITLEBAR", "MAIN_MINIMIZE_BUTTON_DEPRESSED",
                  MainLayout::minimize.x(), MainLayout::minimize.y());
    }
    if (state.pressed.close) {
        skin.blit(painter, "TITLEBAR", "MAIN_CLOSE_BUTTON_DEPRESSED", MainLayout::close.x(),
                  MainLayout::close.y());
    }

    // Seek bar: groove always, thumb only while a track is loaded. Webamp
    // switches the 3px thumb sprite by position class (left/center/right).
    skin.blit(painter, "TITLEBAR", "MAIN_SHADE_POSITION_BACKGROUND",
              ShadeLayout::position.x(), ShadeLayout::position.y());
    if (state.status != PlayerStatus::Stopped) {
        const char* thumb = "MAIN_SHADE_POSITION_THUMB";
        if (state.position <= 33) {
            thumb = "MAIN_SHADE_POSITION_THUMB_LEFT";
        } else if (state.position >= 66) {
            thumb = "MAIN_SHADE_POSITION_THUMB_RIGHT";
        }
        skin.blit(painter, "TITLEBAR", thumb,
                  ShadeLayout::position.x() +
                      thumbLeft(state.position, 0, 100, ShadeLayout::position.width(),
                                SliderMetrics::shadeThumbW),
                  ShadeLayout::position.y());
    }

    // Mini time: five opaque background cells, then the live characters.
    // Slot 0 is the sign, 1-2 the minutes, 3-4 the seconds (Webamp `OC`/`LC`).
    {
        const auto* text = skin.sheet("TEXT");
        if (text != nullptr) {
            // The five background cells (Webamp's `OC`) are opaque and always
            // painted in shade mode, even while stopped.
            for (int slot = 0; slot < 5; ++slot) {
                painter.drawImage(
                    QPoint(ShadeLayout::miniTime.x() + ShadeLayout::miniTimeSlots[slot],
                           ShadeLayout::miniTime.y()),
                    *text, QRect(150, 0, koalaamp::skins::kCharWidth, koalaamp::skins::kCharHeight));
            }
        }
        if (state.status != PlayerStatus::Stopped && state.timeBlinkOn && text != nullptr) {
            const int elapsed = std::max(0, state.timeSeconds);
            const std::string digits = {
                static_cast<char>('0' + (elapsed / 60) / 10 % 10),
                static_cast<char>('0' + (elapsed / 60) % 10),
                static_cast<char>('0' + (elapsed % 60) / 10),
                static_cast<char>('0' + elapsed % 10),
            };
            // Slot 0: the countdown sign (a space otherwise).
            drawGlyph(skin, painter, state.countdown ? U'-' : U' ',
                      ShadeLayout::miniTime.x() + ShadeLayout::miniTimeSlots[0],
                      ShadeLayout::miniTime.y());
            for (int i = 0; i < 4; ++i) {
                drawGlyph(skin, painter,
                          static_cast<char32_t>(digits[static_cast<std::size_t>(i)]),
                          ShadeLayout::miniTime.x() + ShadeLayout::miniTimeSlots[i + 1],
                          ShadeLayout::miniTime.y());
            }
        }
    }
}

}  // namespace

int volumeFrameOffset(int volume) {
    const int clamped = std::clamp(volume, 0, 100);
    // Webamp: sprite = round(percent * 28), offset = (sprite - 1) * 15. At
    // volume 0/1 that yields sprite 0 (offset -15) — an out-of-range frame
    // that both Winamp and Webamp's own CSS fall back to frame 0 for.
    const int sprite = std::max(1, static_cast<int>(std::lround((clamped / 100.0) * 28.0)));
    return (sprite - 1) * 15;
}

int balanceFrameOffset(int balance) {
    const int clamped = std::clamp(balance, -100, 100);
    const int sprite = static_cast<int>(std::floor((std::abs(clamped) / 100.0) * 27.0));
    return sprite * 15;
}

int thumbLeft(int value, int minValue, int maxValue, int trackWidth, int thumbWidth) {
    const int clamped = std::clamp(value, minValue, maxValue);
    const double fraction =
        static_cast<double>(clamped - minValue) / static_cast<double>(maxValue - minValue);
    return static_cast<int>(std::lround(fraction * (trackWidth - thumbWidth)));
}

std::string formatKbps(int kbps) {
    // Webamp `PE(bitrate)` with bitrate in bit/s → n is the kbit/s integer.
    const int n = kbps;
    std::string t = std::to_string(n);
    if (n <= 100) {
        t = std::string(3 - std::min<std::size_t>(3, t.size()), ' ') + t;
    }
    if (n >= 1000) {
        t = std::to_string(n).substr(0, 2) + "H";
    }
    if (n >= 10000) {
        std::string s = std::to_string(n).substr(0, 1);
        t = std::string(2 - std::min<std::size_t>(2, s.size()), ' ') + s + "C";
    }
    return t;
}

std::string formatKhz(int khz) {
    // Webamp `QE(sampleRate)` with the sample rate in Hz / 1000.
    const int n = khz;
    std::string t = std::to_string(n);
    if (n <= 10) {
        t = std::string(2 - std::min<std::size_t>(2, t.size()), ' ') + t.substr(0, 1);
    }
    if (n >= 100) {
        t = t.substr(1, 2);
    }
    return t;
}

QImage renderPlayerState(const Skin& skin, const VisColor& visColor, const PlayerState& state,
                         int scale) {
    const int baseHeight = state.shade ? kShadeHeight : kMainHeight;
    QImage canvas(kMainWidth, baseHeight, QImage::Format_RGB32);
    canvas.fill(Qt::black);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setRenderHint(QPainter::TextAntialiasing, false);

    if (state.shade) {
        drawShadeWindow(skin, painter, state);
    } else {
        drawMainWindowLayout(skin, painter, visColor, state);
    }
    painter.end();

    if (scale <= 1) {
        return canvas;
    }
    // Integer upscale: nearest-neighbour only (docs/spec.md §5.2).
    return canvas.scaled(kMainWidth * scale, baseHeight * scale, Qt::IgnoreAspectRatio,
                         Qt::FastTransformation);
}

QImage renderMainWindow(const Skin& skin, const VisColor& visColor, const PlayerState& state,
                        int scale) {
    return renderPlayerState(skin, visColor, state, scale);
}

QImage renderChrome(const Skin& skin, const VisColor& visColor, const PlayerState& state,
                    int scale) {
    // Everything except the live visualiser columns: the paint path blits this
    // cache and then draws the columns on top (SkinCanvas).
    PlayerState chromeState = state;
    chromeState.visFrame.clear();
    return renderPlayerState(skin, visColor, chromeState, scale);
}

void drawVisualizerOverlay(QPainter& painter, const VisColor& visColor, const PlayerState& state,
                           int scale) {
    drawVisArea(painter, visColor, state, scale, true);
}

}  // namespace koalaamp::ui
