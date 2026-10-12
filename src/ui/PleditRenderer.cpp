// SPDX-License-Identifier: MIT
#include "ui/PleditRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPainter>

#include "skins/SkinSprites.hpp"
#include "ui/TitleText.hpp"

namespace koalaamp::ui {

using koalaamp::skins::fontGlyph;
using koalaamp::skins::PleditColors;
using koalaamp::skins::Skin;
using koalaamp::skins::SpriteRect;

namespace {

constexpr const char* kSheet = "PLEDIT";

QColor rgb(const skins::Rgb& c) { return QColor(c.r, c.g, c.b); }

/// Right-aligned or left-anchored horizontal tiling of a 25 px band tile.
void drawTilesH(const Skin& skin, QPainter& painter, const char* sprite, const QRect& region,
                bool anchorRight) {
    const auto rect = skin.spriteRect(kSheet, sprite);
    if (!rect || region.width() <= 0) {
        return;
    }
    painter.save();
    painter.setClipRect(region);
    if (anchorRight) {
        for (int x = region.right() + 1 - rect->w; x + rect->w > region.left(); x -= rect->w) {
            skin.blit(painter, kSheet, sprite, x, region.top());
        }
    } else {
        for (int x = region.left(); x < region.right() + 1; x += rect->w) {
            skin.blit(painter, kSheet, sprite, x, region.top());
        }
    }
    painter.restore();
}

void drawTilesV(const Skin& skin, QPainter& painter, const char* sprite, const QRect& region) {
    const auto rect = skin.spriteRect(kSheet, sprite);
    if (!rect || region.height() <= 0) {
        return;
    }
    painter.save();
    painter.setClipRect(region);
    for (int y = region.top(); y < region.bottom() + 1; y += rect->h) {
        skin.blit(painter, kSheet, sprite, region.left(), y);
    }
    painter.restore();
}

/// One opaque 5x6 glyph (background included).
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

/// Skin-font text, clipped to `clipWidth` px when > 0.
void drawSkinText(const Skin& skin, QPainter& painter, const std::string& text, int x, int y,
                  int clipWidth) {
    if (clipWidth > 0) {
        painter.save();
        painter.setClipRect(QRect(x, y, clipWidth, koalaamp::skins::kCharHeight));
    }
    int penX = x;
    for (std::size_t i = 0; i < text.size();) {
        const char32_t cp = nextCodepoint(text, i);
        drawGlyph(skin, painter, foldForFont(cp), penX, y);
        penX += koalaamp::skins::kCharWidth;
    }
    if (clipWidth > 0) {
        painter.restore();
    }
}

/// The QFont for a playlist family, or an invalid font when it is absent.
QFont pleditSystemFont(const PleditColors& colors) {
    QFont font;
    if (pleditFontAvailable(colors.font)) {
        font.setFamily(QString::fromStdString(colors.font));
        font.setPixelSize(9);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    }
    return font;
}

/// Draws one text run left-aligned, vertically centred in `rect`, clipped.
void drawRowText(const Skin& skin, QPainter& painter, const QFont& font, const std::string& text,
                 const QRect& rect, const QColor& color) {
    painter.save();
    painter.setClipRect(rect);
    painter.setPen(color);
    if (font.family().isEmpty()) {
        const int y = rect.top() + (rect.height() - koalaamp::skins::kCharHeight) / 2;
        drawSkinText(skin, painter, text, rect.left(), y, rect.width());
    } else {
        painter.setFont(font);
        painter.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter,
                         QString::fromStdString(text));
    }
    painter.restore();
}

void drawRowTextRight(const Skin& skin, QPainter& painter, const QFont& font,
                      const std::string& text, const QRect& rect, const QColor& color) {
    painter.save();
    painter.setClipRect(rect);
    painter.setPen(color);
    if (font.family().isEmpty()) {
        const int width = static_cast<int>(koalaamp::ui::codepointCount(text)) * 5;
        const int y = rect.top() + (rect.height() - koalaamp::skins::kCharHeight) / 2;
        drawSkinText(skin, painter, text, rect.right() + 1 - width, y, rect.width());
    } else {
        painter.setFont(font);
        painter.drawText(rect, Qt::AlignRight | Qt::AlignVCenter,
                         QString::fromStdString(text));
    }
    painter.restore();
}

int scrollFraction(const PleditState& state) {
    const int visible = pleditVisibleTracks(state.heightSegments);
    const int overflow = std::max(0, static_cast<int>(state.entries.size()) - visible);
    if (overflow == 0) {
        return 0;
    }
    return std::clamp(state.scrollOffset, 0, overflow);
}

void drawTitleBand(const Skin& skin, QPainter& painter, const PleditState& state,
                   const PleditColors& colors) {
    (void)colors;
    const bool selected = state.focused;
    const QRect left = pleditTopLeftCorner(state);
    const QRect right = pleditTopRightCorner(state);
    const QRect title = pleditTitleBar(state);
    const int width = pleditWidth(state);
    const bool spacers = state.widthSegments % 2 == 0;
    const int leftSpacer = spacers ? 12 : 0;
    const int rightSpacer = spacers ? 13 : 0;
    const int flex = std::max(0, width - 25 - 25 - 100 - leftSpacer - rightSpacer);
    const int leftFill = flex / 2;
    const QRect leftFillRect(25 + leftSpacer, 0, leftFill, kPleditTopHeight);
    const QRect rightFillRect(title.right() + 1 + rightSpacer, 0,
                              std::max(0, right.left() - title.right() - 1 - rightSpacer),
                              kPleditTopHeight);

    const char* tile = selected ? "PLAYLIST_TOP_TILE_SELECTED" : "PLAYLIST_TOP_TILE";
    drawTilesH(skin, painter, tile, leftFillRect, true);
    drawTilesH(skin, painter, tile, rightFillRect, true);
    skin.blit(painter, kSheet,
              selected ? "PLAYLIST_TOP_LEFT_SELECTED" : "PLAYLIST_TOP_LEFT_CORNER", left.x(),
              left.y());
    skin.blit(painter, kSheet,
              selected ? "PLAYLIST_TITLE_BAR_SELECTED" : "PLAYLIST_TITLE_BAR", title.x(),
              title.y());
    skin.blit(painter, kSheet,
              selected ? "PLAYLIST_TOP_RIGHT_CORNER_SELECTED" : "PLAYLIST_TOP_RIGHT_CORNER",
              right.x(), right.y());

    if (state.shadePressed) {
        skin.blit(painter, kSheet, "PLAYLIST_COLLAPSE_SELECTED",
                  pleditShadeButton(state).x(), pleditShadeButton(state).y());
    }
    if (state.closePressed) {
        skin.blit(painter, kSheet, "PLAYLIST_CLOSE_SELECTED", pleditCloseButton(state).x(),
                  pleditCloseButton(state).y());
    }
}

void drawShadeBand(const Skin& skin, QPainter& painter, const PleditState& state) {
    const int width = pleditWidth(state);
    const auto* rightSprite = state.focused ? "PLAYLIST_SHADE_BACKGROUND_RIGHT_SELECTED"
                                             : "PLAYLIST_SHADE_BACKGROUND_RIGHT";
    const auto rightRect = skin.spriteRect(kSheet, rightSprite);
    const int rightW = rightRect ? rightRect->w : 50;
    drawTilesH(skin, painter, "PLAYLIST_SHADE_BACKGROUND",
               QRect(25, 0, std::max(0, width - 25 - rightW), kPleditShadeHeight), false);
    skin.blit(painter, kSheet, "PLAYLIST_SHADE_BACKGROUND_LEFT", 0, 0);
    if (rightRect) {
        skin.blit(painter, kSheet, rightSprite, width - rightW, 0);
    }

    if (state.shadePressed) {
        skin.blit(painter, kSheet, "PLAYLIST_EXPAND_SELECTED", pleditShadeButton(state).x(),
                  pleditShadeButton(state).y());
    }
    if (state.closePressed) {
        skin.blit(painter, kSheet, "PLAYLIST_CLOSE_SELECTED", pleditCloseButton(state).x(),
                  pleditCloseButton(state).y());
    }
}

void drawMiddle(const Skin& skin, QPainter& painter, const PleditState& state,
                const PleditColors& colors) {
    drawTilesV(skin, painter, "PLAYLIST_LEFT_TILE", pleditMiddleLeft(state));
    drawTilesV(skin, painter, "PLAYLIST_RIGHT_TILE", pleditMiddleRight(state));

    // Rows.
    const QFont font = pleditSystemFont(colors);
    const int visible = pleditVisibleTracks(state.heightSegments);
    const QRect area = pleditListArea(state);
    for (int row = 0; row < visible; ++row) {
        const int index = state.scrollOffset + row;
        if (index >= static_cast<int>(state.entries.size())) {
            break;
        }
        const QRect rect = pleditTrackRow(state, row);
        const PleditRowVisual visual =
            pleditRowVisual(colors, state, static_cast<std::size_t>(index));
        painter.fillRect(rect, visual.background);

        const QRect labelRect(rect.left(), rect.top(), rect.width(), rect.height());
        drawRowText(skin, painter, font, pleditRowLabel(state, static_cast<std::size_t>(index)),
                    labelRect, visual.text);

        const std::string duration = formatPleditDuration(
            state.entries[static_cast<std::size_t>(index)].lengthMs);
        const QRect durationRect(area.right() - 44, rect.top(), 44, rect.height());
        drawRowTextRight(skin, painter, font, duration, durationRect, visual.text);
    }

    // Scrollbar handle.
    const QRect scroll = pleditScrollBar(state);
    if (scroll.width() > 0 && static_cast<int>(state.entries.size()) > visible) {
        const QRect handle = pleditScrollHandle(state);
        skin.blit(painter, kSheet, "PLAYLIST_SCROLL_HANDLE", handle.x(), handle.y());
    }
}

void drawBottom(const Skin& skin, QPainter& painter, const PleditState& state,
                const PleditColors& colors) {
    const QRect bottom = pleditBottom(state);
    const QRect left = pleditBottomLeft(state);
    const QRect right = pleditBottomRight(state);

    // The tiled middle of the bottom band sits between the corners; at the
    // minimum width it is empty.
    const QRect tileRegion(left.right() + 1, bottom.top(),
                           std::max(0, right.left() - left.right() - 1), kPleditBottomHeight);
    drawTilesH(skin, painter, "PLAYLIST_BOTTOM_TILE", tileRegion, false);
    skin.blit(painter, kSheet, "PLAYLIST_BOTTOM_LEFT_CORNER", left.x(), left.y());
    skin.blit(painter, kSheet, "PLAYLIST_BOTTOM_RIGHT_CORNER", right.x(), right.y());
    skin.blit(painter, kSheet, "PLAYLIST_VISUALIZER_BACKGROUND", pleditVisualizer(state).x(),
              pleditVisualizer(state).y());

    // Running total time (right-padded to 18 cells) and the mini track time.
    long totalMs = 0;
    for (const auto& entry : state.entries) {
        totalMs += std::max(0, entry.lengthMs);
    }
    std::string running = formatPleditDuration(static_cast<int>(totalMs)) + "/" +
                          formatPleditDuration(static_cast<int>(totalMs));
    while (running.size() < 18) {
        running.push_back(' ');
    }
    const QFont font = pleditSystemFont(colors);
    const QRect runningRect = pleditRunningTime(state);
    drawRowText(skin, painter, QFont(), running, runningRect, rgb(colors.normal));
    (void)font;

    const QRect mini = pleditMiniTime(state);
    std::string miniText = "--:--";
    if (state.currentIndex >= 0 &&
        state.currentIndex < static_cast<int>(state.entries.size())) {
        miniText = formatPleditDuration(
            state.entries[static_cast<std::size_t>(state.currentIndex)].lengthMs);
    }
    drawSkinText(skin, painter, miniText, mini.x(), mini.y(),
                 mini.width());
    (void)font;
}

}  // namespace

// --- geometry ----------------------------------------------------------------

QRect pleditTopBar(const PleditState& state) {
    return QRect(0, 0, pleditWidth(state), kPleditTopHeight);
}

QRect pleditTopLeftCorner(const PleditState&) {
    return QRect(0, 0, 25, kPleditTopHeight);
}

QRect pleditTopRightCorner(const PleditState& state) {
    return QRect(pleditWidth(state) - 25, 0, 25, kPleditTopHeight);
}

QRect pleditTitleBar(const PleditState& state) {
    const int width = pleditWidth(state);
    const bool spacers = state.widthSegments % 2 == 0;
    const int leftSpacer = spacers ? 12 : 0;
    const int rightSpacer = spacers ? 13 : 0;
    const int flex = std::max(0, width - 25 - 25 - 100 - leftSpacer - rightSpacer);
    const int leftFill = flex / 2;
    return QRect(25 + leftSpacer + leftFill, 0, 100, kPleditTopHeight);
}

QRect pleditCloseButton(const PleditState& state) {
    const int width = pleditWidth(state);
    return QRect(width - 2 - 9, 3, 9, 9);
}

QRect pleditShadeButton(const PleditState& state) {
    const QRect close = pleditCloseButton(state);
    return QRect(close.left() - 10, 3, 9, 9);
}

QRect pleditMiddle(const PleditState& state) {
    return QRect(0, kPleditTopHeight, pleditWidth(state), pleditMiddleHeight(state.heightSegments));
}

QRect pleditMiddleLeft(const PleditState& state) {
    const QRect middle = pleditMiddle(state);
    return QRect(middle.left(), middle.top(), kPleditLeftWidth, middle.height());
}

QRect pleditMiddleRight(const PleditState& state) {
    const QRect middle = pleditMiddle(state);
    return QRect(pleditWidth(state) - kPleditRightWidth, middle.top(), kPleditRightWidth,
                 middle.height());
}

QRect pleditListArea(const PleditState& state) {
    const QRect middle = pleditMiddle(state);
    return QRect(kPleditLeftWidth, middle.top(), pleditWidth(state) - kPleditLeftWidth -
                                                    kPleditRightWidth,
                 middle.height());
}

QRect pleditTrackRow(const PleditState& state, int visibleRow) {
    const QRect area = pleditListArea(state);
    return QRect(area.left() + 2, area.top() + kPleditListPad + visibleRow * kPleditTrackHeight,
                 area.width() - 4, kPleditTrackHeight);
}

QRect pleditScrollBar(const PleditState& state) {
    const QRect middle = pleditMiddle(state);
    return QRect(pleditWidth(state) - 15, middle.top(), 8, middle.height());
}

QRect pleditScrollHandle(const PleditState& state) {
    const QRect bar = pleditScrollBar(state);
    const int visible = pleditVisibleTracks(state.heightSegments);
    const int overflow = std::max(0, static_cast<int>(state.entries.size()) - visible);
    const int travel = std::max(0, bar.height() - 18);
    const int position =
        overflow == 0 ? 0 : static_cast<int>(std::lround(
                                  static_cast<double>(scrollFraction(state)) /
                                  static_cast<double>(overflow) * travel));
    return QRect(bar.x(), bar.top() + position, 8, 18);
}

QRect pleditScrollUp(const PleditState& state) {
    const QRect right = pleditBottomRight(state);
    return QRect(right.right() - 7 - 8, right.top() + 2, 8, 5);
}

QRect pleditScrollDown(const PleditState& state) {
    const QRect right = pleditBottomRight(state);
    return QRect(right.right() - 7 - 8, right.top() + 8, 8, 5);
}

QRect pleditBottom(const PleditState& state) {
    return QRect(0, kPleditTopHeight + pleditMiddleHeight(state.heightSegments),
                 pleditWidth(state), kPleditBottomHeight);
}

QRect pleditBottomLeft(const PleditState&) { return QRect(0, 0, 125, kPleditBottomHeight); }

QRect pleditBottomRight(const PleditState& state) {
    const QRect bottom = pleditBottom(state);
    return QRect(pleditWidth(state) - 150, bottom.top(), 150, kPleditBottomHeight);
}

QRect pleditVisualizer(const PleditState& state) {
    const QRect bottom = pleditBottom(state);
    return QRect(pleditWidth(state) - 225, bottom.top(), 75, kPleditBottomHeight);
}

QRect pleditRunningTime(const PleditState& state) {
    const QRect right = pleditBottomRight(state);
    return QRect(right.left() + 7, right.top() + 10, 18 * 5, 8);
}

QRect pleditMiniTime(const PleditState& state) {
    const QRect right = pleditBottomRight(state);
    return QRect(right.left() + 66, right.top() + 23, 5 * 5, 6);
}

QRect pleditResizeGrip(const PleditState& state) {
    const QRect bottom = pleditBottom(state);
    return QRect(pleditWidth(state) - 20, bottom.top() + kPleditBottomHeight - 20, 20, 20);
}

QRect pleditMenuButton(const PleditState& state, int menu) {
    const QRect bottom = pleditBottom(state);
    const int x = 14 + menu * 29;
    return QRect(x, bottom.top() + kPleditBottomHeight - 12 - 18, 22, 18);
}

QRect pleditActionButton(const PleditState& state, int which) {
    const QRect right = pleditBottomRight(state);
    return QRect(right.left() + 3 + which * 10, right.top() + 22, 10, 10);
}

// --- row visuals -------------------------------------------------------------

PleditRowVisual pleditRowVisual(const PleditColors& colors, const PleditState& state,
                                std::size_t index) {
    PleditRowVisual visual;
    visual.background = rgb(colors.normalBg);
    visual.text = rgb(colors.normal);
    visual.queued = queuePosition(state, index) != 0;
    const bool selectedRow = isSelected(state, index);
    const bool current = state.currentIndex == static_cast<int>(index);
    const bool hover = state.hoverIndex == static_cast<int>(index);
    if (selectedRow) {
        visual.background = rgb(colors.selectedBg);
    }
    if (current || (hover && !selectedRow)) {
        visual.text = rgb(colors.current);
    }
    return visual;
}

std::string pleditRowLabel(const PleditState& state, std::size_t index) {
    if (index >= state.entries.size()) {
        return {};
    }
    std::string label = std::to_string(index + 1) + ". " + state.entries[index].title;
    const std::size_t queued = queuePosition(state, index);
    if (queued != 0) {
        label += " ^" + std::to_string(queued);
    }
    return label;
}

std::string formatPleditDuration(int lengthMs) {
    if (lengthMs <= 0) {
        return "--:--";
    }
    const int seconds = lengthMs / 1000;
    const int minutes = seconds / 60;
    const int remainder = seconds % 60;
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, remainder);
    return buffer;
}

// --- text --------------------------------------------------------------------

bool pleditFontAvailable(const std::string& family) {
    if (family.empty() || QGuiApplication::instance() == nullptr) {
        return false;
    }
    return QFontDatabase::families().contains(QString::fromStdString(family));
}

// --- rendering ---------------------------------------------------------------

QImage renderPleditWindow(const Skin& skin, const PleditColors& colors, const PleditState& state,
                          int scale) {
    const int width = pleditWidth(state);
    const int height = state.shade ? kPleditShadeHeight : pleditHeight(state);
    QImage canvas(width, height, QImage::Format_RGB32);
    canvas.fill(rgb(colors.normalBg));

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setRenderHint(QPainter::TextAntialiasing, false);

    if (state.shade) {
        drawShadeBand(skin, painter, state);
    } else {
        drawTitleBand(skin, painter, state, colors);
        drawMiddle(skin, painter, state, colors);
        drawBottom(skin, painter, state, colors);
    }
    painter.end();

    if (scale <= 1) {
        return canvas;
    }
    return canvas.scaled(width * scale, height * scale, Qt::IgnoreAspectRatio,
                         Qt::FastTransformation);
}

}  // namespace koalaamp::ui
