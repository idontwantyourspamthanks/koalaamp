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
/// The colours a gen window's frame is painted with when the skin ships no `gen.bmp`.
struct GenPalette {
    int frame = 0;
    int titleActive = 0;
    int titleInactive = 0;
    int edge = 0;
    int close = 0;
    int text = 0;
    int client = 0;
};

/// Neutral greys: what a skin with neither `gen.bmp` nor `pledit.txt` gets, and what
/// the window paints before any skin is chosen.
GenPalette neutralPalette() {
    GenPalette p;
    p.frame = 0x3C3C46;
    p.titleActive = 0x2A2A32;
    p.titleInactive = 0x232329;
    p.edge = 0x6E6E7E;
    p.close = 0x4A4A55;
    p.text = 0xC8C8D0;
    p.client = 0x000000;
    return p;
}

int rgbOf(const skins::Rgb& c) { return (c.r << 16) | (c.g << 8) | c.b; }

/// Per-channel scale: <1 darkens, >1 lightens. Named for what it does at both ends —
/// the frame's derived shades go both ways.
int scale(int rgb, double factor) {
    const auto channel = [factor](int v) {
        const int scaled = static_cast<int>(v * factor);
        return scaled < 0 ? 0 : (scaled > 255 ? 255 : scaled);
    };
    return (channel((rgb >> 16) & 0xFF) << 16) | (channel((rgb >> 8) & 0xFF) << 8) |
           channel(rgb & 0xFF);
}

/// A gen window's closest sibling is the playlist window, so when the skin has no
/// `gen.bmp` of its own the frame borrows the playlist's palette: the frame is the
/// playlist's window background, the title bar that frame raised a step, the title
/// text the playlist's current-row colour.
///
/// Deliberately *not* `selectedBg` for the title bar: that is the selection highlight,
/// and a saturated selection blue above the skin's own chrome reads as a foreign strip
/// (it did, in the first cut). Steps keep the frame in the skin's family.
GenPalette paletteFromPledit(const skins::PleditColors& pledit) {
    GenPalette p;
    const int frame = rgbOf(pledit.normalBg);
    p.frame = frame;
    p.titleActive = scale(frame, 1.6);
    p.titleInactive = scale(frame, 0.85);
    p.edge = scale(frame, 2.2);
    p.close = scale(p.titleActive, 0.8);
    p.text = rgbOf(pledit.current);
    p.client = 0x000000;
    return p;
}

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

/// The bottom bar. The atlas stores it as two 125 px *variants* of one bar — a
/// light one and a vivid blue one, the same way the title bar has two rows — not
/// as a left end and a right end. Butting them (the earlier guess) produced a
/// two-tone bar with a seam at x=69; instead one variant runs the full width and
/// the fill tiles continue it.
void drawBottomBar(const Skin& skin, QPainter& painter, const QRect& bottom, bool active) {
    // The atlas stores the bar as two stacked full-length variants (selected and
    // unselected, the same way the title bar has two rows), each carrying cyan
    // tile markers every 25 px -- verified against a real skin's gen.bmp, where the
    // bottom rows start at x=0 and the markers sit at x=25/50/75. So: one variant as
    // the bar's left end, then the fill unit tiled across the rest. The seams this
    // used to show came from the fill sprite being drawn as a bordered standalone
    // piece instead of an edge-free tile unit; that is the generator's job.
    const char* bar = active ? "GEN_BOTTOM_LEFT" : "GEN_BOTTOM_RIGHT";
    const int barWidth = std::min(kBottomHalfWidth, bottom.width());
    blitGen(skin, painter, bar, bottom.x(), bottom.y(), barWidth, bottom.height());

    for (int x = bottom.x() + barWidth; x < bottom.x() + bottom.width();) {
        const int step = std::min(kGenSegment, bottom.x() + bottom.width() - x);
        blitGen(skin, painter, "GEN_BOTTOM_FILL", x, bottom.y(), step, bottom.height());
        x += step;
    }
}

/// The fallback title bar and frame, painted in `palette` (the skin's playlist colours
/// when it ships `pledit.txt`, neutral greys otherwise).
void paintFallbackFrame(QPainter& painter, const QSize& size, const GenLayout& layout,
                        bool active, const GenPalette& palette) {
    painter.fillRect(layout.title, QColor(active ? palette.titleActive : palette.titleInactive));
    painter.fillRect(layout.leftBorder, QColor(palette.frame));
    painter.fillRect(layout.rightBorder, QColor(palette.frame));
    painter.fillRect(layout.bottom, QColor(palette.frame));
    painter.fillRect(layout.client, QColor(palette.client));

    // A single light inner edge, the way the skinned frame's top-left highlight
    // reads.
    painter.fillRect(QRect(size.width() / 2, layout.title.bottom(), 1, 1), QColor(palette.edge));
    painter.fillRect(QRect(layout.title.x(), layout.title.top(), size.width(), 1),
                     QColor(palette.edge));
    painter.fillRect(layout.closeButton, QColor(palette.close));
}

/// The window title: the skin's 5x6 uppercase font when it has one, otherwise
/// the fallback font.
void drawTitle(const Skin& skin, QPainter& painter, const GenLayout& layout, const std::string& text,
               int clipRight, const GenPalette& palette) {
    const int y = layout.title.top() + (layout.title.height() - koalaamp::skins::kCharHeight) / 2;
    // After the 25 px left corner sprite: starting at the border width put the text
    // on top of the corner art.
    int penX = layout.title.x() + kGenSegment + 3;
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
    painter.setPen(QColor(palette.text));
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

QImage renderGenWindow(const skins::Skin& skin, const skins::PleditColors* pledit,
                       const QSize& contentSize, const std::string& title, bool active,
                       int scale) {
    const QSize size = genSnapSize(contentSize);
    const GenLayout layout = genLayout(size);
    // A skin without `gen.bmp` still needs a frame that belongs to the skin, so the
    // fallback takes the playlist's colours when the caller has them.
    const GenPalette palette = pledit != nullptr ? paletteFromPledit(*pledit) : neutralPalette();
    QImage image(size, QImage::Format_RGB32);
    image.fill(QColor(palette.client));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (genUsesFallback(skin)) {
        paintFallbackFrame(painter, size, layout, active, palette);
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
        drawBottomBar(skin, painter, layout.bottom, active);
        blitGen(skin, painter, "GEN_CLOSE_SELECTED", layout.closeButton.x(), layout.closeButton.y(),
                layout.closeButton.width(), layout.closeButton.height());
    }

    drawTitle(skin, painter, layout, title, layout.closeButton.left() - kGenCloseMargin, palette);
    painter.end();

    if (scale > 1) {
        image = image.scaled(size * scale, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    return image;
}

}  // namespace koalaamp::ui
