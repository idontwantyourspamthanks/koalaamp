// SPDX-License-Identifier: MIT
//
// GenRenderer — see GenRenderer.hpp for the geometry rules and sources.
#include "ui/GenRenderer.hpp"

#include <algorithm>

#include <QColor>
#include <QFont>
#include <QPainter>

#include "skins/SkinSprites.hpp"
#include "ui/TitleText.hpp"

namespace koalaamp::ui {
namespace {

using koalaamp::skins::Skin;
using koalaamp::skins::SpriteRect;

constexpr char kSheetGen[] = "GEN";
constexpr char kSheetText[] = "TEXT";

/// Height of the `GEN_MIDDLE_*_BOTTOM` cap sprites, which close the side
/// columns.
constexpr int kBorderCapHeight = 24;
/// Width of the `GEN_BOTTOM_*` halves.
constexpr int kBottomHalfWidth = 125;
/// Fallback frame palette (used when the skin ships no `gen.bmp`).
constexpr int kFallbackClient = 0x000000;
constexpr int kFallbackFrame = 0x3C3C46;
constexpr int kFallbackTitleActive = 0x2A2A32;
constexpr int kFallbackTitleInactive = 0x232329;
constexpr int kFallbackEdge = 0x6E6E7E;
constexpr int kFallbackClose = 0x4A4A55;
constexpr int kFallbackText = 0xC8C8D0;

bool blitGen(const Skin& skin, QPainter& painter, const char* sprite, int x, int y, int clipW,
             int clipH) {
    return skin.blit(painter, kSheetGen, sprite, x, y, clipW, clipH);
}

/// Repeats a horizontal fill sprite from `x` to `x + width` (1x pixels), the
/// last tile clipped to what remains.
void tileHorizontal(const Skin& skin, QPainter& painter, const char* sprite, int x, int y,
                    int width) {
    for (int left = x; left < x + width;) {
        const int step = std::min(kGenSegment, x + width - left);
        blitGen(skin, painter, sprite, left, y, step, kGenTitleHeight);
        left += step;
    }
}

/// Fills a side column with `fill` (29 px tiles) and closes it with `cap` when
/// there is room for both — the atlas' 24 px `*_BOTTOM` piece.
void drawSideBorder(const Skin& skin, QPainter& painter, const char* fill, const char* cap, int x,
                    int width, int top, int bottom) {
    const bool hasCap = (bottom - top) >= (kGenSegmentHeight + kBorderCapHeight);
    const int fillEnd = hasCap ? bottom - kBorderCapHeight : bottom;
    for (int y = top; y < fillEnd;) {
        const int step = std::min(kGenSegmentHeight, fillEnd - y);
        skin.blit(painter, kSheetGen, fill, x, y, width, step);
        y += step;
    }
    if (hasCap) {
        skin.blit(painter, kSheetGen, cap, x, bottom - kBorderCapHeight, width, kBorderCapHeight);
    }
}

/// The bottom bar: the two 125 px halves butted against the ends, the fill
/// sprites repeated in between. On a window narrower than 250 px the halves
/// overlap and nothing is left to fill (the reference width is 194 px, so this
/// is the common case).
void drawBottomBar(const Skin& skin, QPainter& painter, const QRect& bottom) {
    const int leftWidth = std::min(kBottomHalfWidth, bottom.width() - kGenRightBorder);
    blitGen(skin, painter, "GEN_BOTTOM_LEFT", bottom.x(), bottom.y(), leftWidth, bottom.height());

    const int rightX = std::max(bottom.x(), bottom.x() + bottom.width() - kBottomHalfWidth);
    blitGen(skin, painter, "GEN_BOTTOM_RIGHT", rightX, bottom.y(),
            bottom.x() + bottom.width() - rightX, bottom.height());

    for (int x = bottom.x() + leftWidth; x < rightX;) {
        const int step = std::min(kGenSegment, rightX - x);
        blitGen(skin, painter, "GEN_BOTTOM_FILL", x, bottom.y(), step, bottom.height());
        x += step;
    }
}

/// The fallback title bar and frame: neutral greys, so a skin without `gen.bmp`
/// still gets a window that reads as one.
void paintFallbackFrame(QPainter& painter, const QSize& size, const GenLayout& layout,
                        bool active) {
    painter.fillRect(layout.title, QColor(active ? kFallbackTitleActive : kFallbackTitleInactive));
    painter.fillRect(layout.leftBorder, QColor(kFallbackFrame));
    painter.fillRect(layout.rightBorder, QColor(kFallbackFrame));
    painter.fillRect(layout.bottom, QColor(kFallbackFrame));
    painter.fillRect(layout.client, QColor(kFallbackClient));

    // A single light inner edge, the way the skinned frame's top-left highlight
    // reads.
    painter.fillRect(QRect(size.width() / 2, layout.title.bottom(), 1, 1), QColor(kFallbackEdge));
    painter.fillRect(QRect(layout.title.x(), layout.title.top(), size.width(), 1),
                     QColor(kFallbackEdge));
    painter.fillRect(layout.closeButton, QColor(kFallbackClose));
}

/// The window title: the skin's 5x6 uppercase font when it has one, otherwise
/// the fallback font.
void drawTitle(const Skin& skin, QPainter& painter, const GenLayout& layout, const std::string& text,
               int clipRight) {
    const int y = layout.title.top() + (layout.title.height() - koalaamp::skins::kCharHeight) / 2;
    int penX = layout.title.x() + kGenLeftBorder + 2;
    if (!text.empty() && skin.sheet(kSheetText) != nullptr) {
        painter.save();
        painter.setClipRect(QRect(penX, y, std::max(0, clipRight - penX),
                                  koalaamp::skins::kCharHeight));
        for (std::size_t i = 0; i < text.size();) {
            const char32_t cp = nextCodepoint(text, i);
            if (const auto glyph = koalaamp::skins::fontGlyph(foldForFont(cp))) {
                const QImage* sheet = skin.sheet(kSheetText);
                painter.drawImage(QPoint(penX, y), *sheet,
                                  QRect(glyph->x, glyph->y, glyph->w, glyph->h));
            }
            penX += koalaamp::skins::kCharWidth;
        }
        painter.restore();
        return;
    }

    if (text.empty()) {
        return;
    }
    QFont font;
    font.setPixelSize(koalaamp::skins::kCharHeight + 1);
    painter.setFont(font);
    painter.setPen(QColor(kFallbackText));
    painter.drawText(QRect(penX, layout.title.top(), std::max(0, clipRight - penX),
                           layout.title.height()),
                     Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(text));
}

}  // namespace

GenLayout genLayout(const QSize& contentSize) {
    const int w = contentSize.width();
    const int h = contentSize.height();

    GenLayout layout;
    layout.title = QRect(0, 0, w, kGenTitleHeight);
    layout.closeButton =
        QRect(w - kGenRightBorder - kGenCloseMargin - kGenCloseSize,
              (kGenTitleHeight - kGenCloseSize) / 2, kGenCloseSize, kGenCloseSize);
    layout.leftBorder = QRect(0, kGenTitleHeight, kGenLeftBorder, h - kGenTitleHeight);
    layout.rightBorder = QRect(w - kGenRightBorder, kGenTitleHeight, kGenRightBorder,
                               h - kGenTitleHeight);
    layout.bottom = QRect(0, h - kGenBottomHeight, w, kGenBottomHeight);
    layout.leftBorder.setBottom(layout.bottom.top());
    layout.rightBorder.setBottom(layout.bottom.top());
    layout.client = QRect(kGenLeftBorder, kGenTitleHeight, w - kGenLeftBorder - kGenRightBorder,
                          h - kGenTitleHeight - kGenBottomHeight);
    return layout;
}

QSize genSnapSize(const QSize& wanted) {
    const int w = std::max(kGenWidth, wanted.width());
    const int h = std::max(kGenHeight, wanted.height());
    const int stepsX = (w - kGenWidth + kGenSegment - 1) / kGenSegment;
    const int stepsY = (h - kGenHeight + kGenSegmentHeight - 1) / kGenSegmentHeight;
    return QSize(kGenWidth + stepsX * kGenSegment, kGenHeight + stepsY * kGenSegmentHeight);
}

bool genUsesFallback(const skins::Skin& skin) {
    return skin.sheet(kSheetGen) == nullptr;
}

QImage renderGenWindow(const skins::Skin& skin, const QSize& contentSize, const std::string& title,
                       bool active, int scale) {
    const QSize size = genSnapSize(contentSize);
    const GenLayout layout = genLayout(size);
    QImage image(size, QImage::Format_RGB32);
    image.fill(QColor(kFallbackClient));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (genUsesFallback(skin)) {
        paintFallbackFrame(painter, size, layout, active);
    } else {
        // Title bar: corners plus the repeated 25 px fill.
        const char* left = active ? "GEN_TOP_LEFT_SELECTED" : "GEN_TOP_LEFT";
        const char* fill = active ? "GEN_TOP_CENTER_FILL_SELECTED" : "GEN_TOP_CENTER_FILL";
        const char* right = active ? "GEN_TOP_RIGHT_SELECTED" : "GEN_TOP_RIGHT";
        const int cornerWidth = std::min(kGenSegment, size.width() / 2);
        blitGen(skin, painter, left, 0, 0, cornerWidth, kGenTitleHeight);
        if (size.width() > 2 * cornerWidth) {
            tileHorizontal(skin, painter, fill, cornerWidth, 0, size.width() - 2 * cornerWidth);
            blitGen(skin, painter, right, size.width() - cornerWidth, 0, cornerWidth,
                    kGenTitleHeight);
        }

        drawSideBorder(skin, painter, "GEN_MIDDLE_LEFT", "GEN_MIDDLE_LEFT_BOTTOM",
                       layout.leftBorder.x(), layout.leftBorder.width(), layout.leftBorder.top(),
                       layout.leftBorder.bottom() + 1);
        drawSideBorder(skin, painter, "GEN_MIDDLE_RIGHT", "GEN_MIDDLE_RIGHT_BOTTOM",
                       layout.rightBorder.x(), layout.rightBorder.width(), layout.rightBorder.top(),
                       layout.rightBorder.bottom() + 1);
        drawBottomBar(skin, painter, layout.bottom);
        blitGen(skin, painter, "GEN_CLOSE_SELECTED", layout.closeButton.x(), layout.closeButton.y(),
                layout.closeButton.width(), layout.closeButton.height());
    }

    drawTitle(skin, painter, layout, title, layout.closeButton.left() - kGenCloseMargin);
    painter.end();

    if (scale > 1) {
        image = image.scaled(size * scale, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    return image;
}

}  // namespace koalaamp::ui
