// SPDX-License-Identifier: MIT
//
// GenRenderer — see GenRenderer.hpp for the geometry rules and sources.
#include "ui/GenRenderer.hpp"

#include <algorithm>
#include <array>

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
/// Where the gen alphabet sits. The frame pieces end at the bottom-fill row
/// (y=72, 14 px), and the two 7 px alphabets follow: active, then inactive.
/// The delimiter is whatever solid colour the sheet put in the column before
/// 'A' — letters must not use it, so the widths fall out of one scan row.
constexpr int kGenFontHeight = 7;
constexpr int kGenFontActiveY = 88;
constexpr int kGenFontInactiveY = 96;
constexpr int kGenFontScanY = kGenFontActiveY + 2;
/// The alphabet is blitted onto the title plate. y=4 lands the 7 px glyphs in
/// the plate's flat middle (the plate is 20 px; the side bars' bright groove
/// is the stretch tile, not the plate).
constexpr int kGenTitleTextY = 4;
/// A little air inside the title plate, either side of the glyphs.
constexpr int kGenTitlePad = 12;
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

/// The bottom bar: the 125 px left corner, the 25 px fill tiled across the
/// opening, and the 125 px right corner (the one with the resize grip). The
/// two corners are stacked in the sheet because both are wider than the spare
/// columns beside the side walls — they are not active/inactive variants.
void drawBottomBar(const Skin& skin, QPainter& painter, const QRect& bottom) {
    const int width = bottom.width();
    if (width >= kGenBottomCorner * 2) {
        blitGen(skin, painter, "GEN_BOTTOM_LEFT", bottom.x(), bottom.y(), kGenBottomCorner,
                bottom.height());
        blitGen(skin, painter, "GEN_BOTTOM_RIGHT", bottom.x() + width - kGenBottomCorner,
                bottom.y(), kGenBottomCorner, bottom.height());
        for (int x = bottom.x() + kGenBottomCorner; x < bottom.x() + width - kGenBottomCorner;) {
            const int step =
                std::min(kGenSegment, bottom.x() + width - kGenBottomCorner - x);
            blitGen(skin, painter, "GEN_BOTTOM_FILL", x, bottom.y(), step, bottom.height());
            x += step;
        }
        return;
    }
    // Narrower than the two corners: keep each outer end and let them meet.
    const int rightX = bottom.x() + width - kGenBottomCorner;
    blitGen(skin, painter, "GEN_BOTTOM_LEFT", bottom.x(), bottom.y(),
            std::max(0, rightX - bottom.x()), bottom.height());
    blitGen(skin, painter, "GEN_BOTTOM_RIGHT", rightX, bottom.y(), kGenBottomCorner,
            bottom.height());
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

struct GenGlyph {
    int x = 0;
    int w = 0;
};

struct GenFont {
    bool valid = false;
    std::array<GenGlyph, 26> letters{};
};

bool columnIsBlank(const QImage& image, int x, int y, int height) {
    if (x < 0 || x >= image.width()) {
        return true;
    }
    for (int row = 0; row < height; ++row) {
        if (y + row >= image.height()) {
            return true;
        }
        if (image.pixel(x, y + row) != qRgb(0, 0, 0)) {
            return false;
        }
    }
    return true;
}

/// The sheet's title alphabet, or `valid == false` when the sheet has none
/// (a flat fill where the letters should be, or a sheet too short to hold them).
GenFont parseGenFont(const QImage& image) {
    GenFont font;
    if (image.height() < kGenFontInactiveY + kGenFontHeight || image.width() < 2) {
        return font;
    }
    const QRgb delimiter = image.pixel(0, kGenFontScanY);
    int x = 0;
    const int width = image.width();
    for (int letter = 0; letter < 26; ++letter) {
        int guard = 0;
        while (x < width && image.pixel(x, kGenFontScanY) == delimiter) {
            ++x;
            if (++guard > 40) {
                return font;
            }
        }
        if (x >= width) {
            return font;
        }
        const int origin = x;
        int glyphWidth = 0;
        while (x < width && image.pixel(x, kGenFontScanY) != delimiter) {
            ++x;
            ++glyphWidth;
            if (glyphWidth > 40) {
                return font;
            }
        }
        // The last letter often runs into the sheet's black padding instead of
        // a closing delimiter. That padding is not part of the glyph; an
        // interior black column (some fonts use black as the cell background)
        // still is, because a real delimiter follows it.
        const bool closed = x < width && image.pixel(x, kGenFontScanY) == delimiter;
        if (!closed) {
            while (glyphWidth > 0 &&
                   columnIsBlank(image, origin + glyphWidth - 1, kGenFontActiveY, kGenFontHeight)) {
                --glyphWidth;
            }
        }
        if (glyphWidth <= 0) {
            return font;
        }
        font.letters[static_cast<std::size_t>(letter)] = GenGlyph{origin, glyphWidth};
    }
    font.valid = true;
    return font;
}

int genFontWidth(const GenFont& font, const std::string& text) {
    int width = 0;
    for (std::size_t i = 0; i < text.size();) {
        char32_t cp = nextCodepoint(text, i);
        if (cp >= U'a' && cp <= U'z') {
            cp = cp - U'a' + U'A';
        }
        if (cp >= U'A' && cp <= U'Z') {
            width += font.letters[static_cast<std::size_t>(cp - U'A')].w;
        } else {
            width += 4;
        }
    }
    return width;
}

int bitmapFontWidth(const std::string& text) {
    int cells = 0;
    for (std::size_t i = 0; i < text.size();) {
        nextCodepoint(text, i);
        ++cells;
    }
    return cells * koalaamp::skins::kCharWidth;
}

void drawGenFontText(const Skin& skin, QPainter& painter, const GenFont& font, bool active,
                     const std::string& text, int x, int clipRight) {
    const QImage* sheet = skin.sheet(kSheetGen);
    if (sheet == nullptr) {
        return;
    }
    const int srcY = active ? kGenFontActiveY : kGenFontInactiveY;
    painter.save();
    painter.setClipRect(QRect(x, kGenTitleTextY, std::max(0, clipRight - x), kGenFontHeight));
    for (std::size_t i = 0; i < text.size();) {
        char32_t cp = nextCodepoint(text, i);
        int advance = 4;
        if (cp >= U'a' && cp <= U'z') {
            cp = cp - U'a' + U'A';
        }
        if (cp >= U'A' && cp <= U'Z') {
            const GenGlyph& glyph = font.letters[static_cast<std::size_t>(cp - U'A')];
            advance = glyph.w;
            if (x + advance > clipRight) {
                break;
            }
            painter.drawImage(QPoint(x, kGenTitleTextY), *sheet,
                              QRect(glyph.x, srcY, glyph.w, kGenFontHeight));
        }
        x += advance;
    }
    painter.restore();
}

/// The space cell in `text.bmp` is the cell background (often a flat colour,
/// sometimes a tight dither). A title drawn on a bar keeps the letter ink and
/// drops that background, including the anti-aliased fringe a few steps away
/// from it. A cell whose colours sprawl is a picture, not a background, and
/// the glyphs stay opaque.
struct TextKey {
    bool active = false;
    int r = 0;
    int g = 0;
    int b = 0;
};

/// Chebyshev distance. The fringe around a dithered cell sits inside this;
/// letter ink (green-on-black, white-on-grey) sits well outside it.
constexpr int kTextKeyTolerance = 40;
constexpr int kTextKeyMaxSpan = 48;

bool keyHas(const TextKey& key, QRgb color) {
    if (!key.active) {
        return false;
    }
    const int dr = std::abs(qRed(color) - key.r);
    const int dg = std::abs(qGreen(color) - key.g);
    const int db = std::abs(qBlue(color) - key.b);
    return std::max(dr, std::max(dg, db)) <= kTextKeyTolerance;
}

TextKey textCellBackground(const QImage& sheet) {
    TextKey key;
    const auto space = koalaamp::skins::fontGlyph(U' ');
    if (!space) {
        return key;
    }
    const QRect cell(space->x, space->y, space->w, space->h);
    if (!sheet.rect().contains(cell)) {
        return key;
    }
    int r0 = 255, g0 = 255, b0 = 255, r1 = 0, g1 = 0, b1 = 0;
    int rs = 0, gs = 0, bs = 0, n = 0;
    for (int y = 0; y < cell.height(); ++y) {
        for (int x = 0; x < cell.width(); ++x) {
            const QRgb color = sheet.pixel(cell.left() + x, cell.top() + y);
            const int r = qRed(color), g = qGreen(color), b = qBlue(color);
            r0 = std::min(r0, r);
            g0 = std::min(g0, g);
            b0 = std::min(b0, b);
            r1 = std::max(r1, r);
            g1 = std::max(g1, g);
            b1 = std::max(b1, b);
            rs += r;
            gs += g;
            bs += b;
            ++n;
        }
    }
    if (n == 0 || r1 - r0 > kTextKeyMaxSpan || g1 - g0 > kTextKeyMaxSpan ||
        b1 - b0 > kTextKeyMaxSpan) {
        return key;
    }
    key.r = rs / n;
    key.g = gs / n;
    key.b = bs / n;
    key.active = true;
    // A low-contrast font whose ink sits inside the tolerance would vanish.
    // If 'A' has nothing left once the background is removed, keep the cells.
    if (const auto probe = koalaamp::skins::fontGlyph(U'a')) {
        const QRect glyph(probe->x, probe->y, probe->w, probe->h);
        if (sheet.rect().contains(glyph)) {
            bool ink = false;
            for (int y = 0; y < glyph.height() && !ink; ++y) {
                for (int x = 0; x < glyph.width(); ++x) {
                    if (!keyHas(key, sheet.pixel(glyph.left() + x, glyph.top() + y))) {
                        ink = true;
                        break;
                    }
                }
            }
            if (!ink) {
                key.active = false;
            }
        }
    }
    return key;
}

void blitKeyedGlyph(QPainter& painter, const QImage& sheet, const QRect& cell, int x, int y,
                    const TextKey& key) {
    if (!key.active) {
        painter.drawImage(QPoint(x, y), sheet, cell);
        return;
    }
    QImage glyph = sheet.copy(cell).convertToFormat(QImage::Format_ARGB32);
    for (int row = 0; row < glyph.height(); ++row) {
        for (int col = 0; col < glyph.width(); ++col) {
            if (keyHas(key, glyph.pixel(col, row))) {
                glyph.setPixel(col, row, qRgba(0, 0, 0, 0));
            }
        }
    }
    painter.drawImage(QPoint(x, y), glyph);
}

/// Centred 5x6 title, for a gen sheet that has no alphabet of its own. `area`
/// is the plate the glyphs sit in (the whole bar, on the fallback frame).
void drawBitmapTitle(const Skin& skin, QPainter& painter, const QRect& area,
                     const std::string& text, const GenPalette& palette) {
    if (text.empty() || area.width() <= 0) {
        return;
    }
    const int y = area.top() + (area.height() - koalaamp::skins::kCharHeight) / 2;
    if (const QImage* sheet = skin.sheet(kSheetText)) {
        const TextKey key = textCellBackground(*sheet);
        const int textWidth = bitmapFontWidth(text);
        int penX = area.left() + std::max(0, (area.width() - textWidth) / 2);
        painter.save();
        painter.setClipRect(QRect(area.left(), y, area.width(), koalaamp::skins::kCharHeight));
        for (std::size_t i = 0; i < text.size();) {
            const char32_t cp = nextCodepoint(text, i);
            if (const auto glyph = koalaamp::skins::fontGlyph(foldForFont(cp))) {
                blitKeyedGlyph(painter, *sheet, QRect(glyph->x, glyph->y, glyph->w, glyph->h),
                               penX, y, key);
            }
            penX += koalaamp::skins::kCharWidth;
        }
        painter.restore();
        return;
    }

    QFont font;
    font.setPixelSize(koalaamp::skins::kCharHeight + 1);
    painter.setFont(font);
    painter.setPen(QColor(palette.text));
    painter.drawText(area, Qt::AlignVCenter | Qt::AlignHCenter, QString::fromStdString(text));
}

/// Title bar: corners, the stretch tile between them, then the centred plate
/// (with its two transition pieces) and the name.
void drawTitleBar(const Skin& skin, QPainter& painter, const QSize& size, const GenLayout& layout,
                  const std::string& title, bool active, const GenPalette& palette,
                  bool closePressed) {
    const char* left = active ? "GEN_TOP_LEFT_SELECTED" : "GEN_TOP_LEFT";
    const char* leftEnd = active ? "GEN_TOP_LEFT_END_SELECTED" : "GEN_TOP_LEFT_END";
    const char* plateTile = active ? "GEN_TOP_CENTER_FILL_SELECTED" : "GEN_TOP_CENTER_FILL";
    const char* rightEnd = active ? "GEN_TOP_RIGHT_END_SELECTED" : "GEN_TOP_RIGHT_END";
    const char* stretch = active ? "GEN_TOP_LEFT_RIGHT_FILL_SELECTED" : "GEN_TOP_LEFT_RIGHT_FILL";
    const char* right = active ? "GEN_TOP_RIGHT_SELECTED" : "GEN_TOP_RIGHT";

    const int corner = std::min(kGenSegment, size.width() / 2);
    blitGen(skin, painter, left, 0, 0, corner, kGenTitleHeight);
    if (size.width() > 2 * corner) {
        tileHorizontal(skin, painter, stretch, corner, 0, size.width() - 2 * corner);
        blitGen(skin, painter, right, size.width() - corner, 0, corner, kGenTitleHeight);
    }

    const QImage* sheet = skin.sheet(kSheetGen);
    const GenFont font = sheet != nullptr ? parseGenFont(*sheet) : GenFont{};
    const int textWidth = title.empty() ? 0
                          : font.valid   ? genFontWidth(font, title)
                                         : bitmapFontWidth(title);
    const int room = size.width() - 2 * corner;
    if (textWidth > 0 && room >= kGenSegment) {
        int plate = ((textWidth + kGenTitlePad + kGenSegment - 1) / kGenSegment) * kGenSegment;
        const int maxPlate = (room / kGenSegment) * kGenSegment;
        plate = std::clamp(plate, kGenSegment, std::max(kGenSegment, maxPlate));
        int plateX = (size.width() - plate) / 2;
        if (plateX < corner) {
            plateX = corner;
        }
        if (plateX + plate > size.width() - corner) {
            plate = size.width() - corner - plateX;
        }
        if (plateX - kGenSegment >= corner) {
            blitGen(skin, painter, leftEnd, plateX - kGenSegment, 0, kGenSegment, kGenTitleHeight);
        }
        tileHorizontal(skin, painter, plateTile, plateX, 0, plate);
        if (plateX + plate + kGenSegment <= size.width() - corner) {
            blitGen(skin, painter, rightEnd, plateX + plate, 0, kGenSegment, kGenTitleHeight);
        }
        if (font.valid) {
            const int textX = plateX + std::max(0, (plate - textWidth) / 2);
            drawGenFontText(skin, painter, font, active, title, textX, plateX + plate);
        } else {
            drawBitmapTitle(skin, painter, QRect(plateX, 0, plate, kGenTitleHeight), title, palette);
        }
    }

    if (closePressed) {
        blitGen(skin, painter, "GEN_CLOSE_SELECTED", layout.closeButton.x(), layout.closeButton.y(),
                layout.closeButton.width(), layout.closeButton.height());
    }
}

constexpr char kSheetPledit[] = "PLEDIT";
/// The playlist bottom band is 38 px of buttons. Its lower 14 px, away from
/// those buttons, is the window edge: a plain bevel in the tile, the corner
/// join on the left, and the resize grip in the last columns on the right.
constexpr int kPleditBottomLeftJoin = 12;
constexpr int kPleditBottomGrip = 22;

bool pleditSprite(const Skin& skin, const char* name, SpriteRect* out) {
    const auto rect = skin.spriteRect(kSheetPledit, name);
    if (!rect) {
        return false;
    }
    *out = *rect;
    return true;
}

/// True when `pledit.bmp` holds every piece the fallback frame is built from.
/// `spriteRect` still reports the table entry when the sheet itself is absent,
/// so the sheet has to be checked first — otherwise a skin with no bitmaps at
/// all would take this path and paint nothing.
bool pleditFrameAvailable(const Skin& skin) {
    if (skin.sheet(kSheetPledit) == nullptr) {
        return false;
    }
    SpriteRect ignored;
    return pleditSprite(skin, "PLAYLIST_TOP_TILE_SELECTED", &ignored) &&
           pleditSprite(skin, "PLAYLIST_TOP_TILE", &ignored) &&
           pleditSprite(skin, "PLAYLIST_TOP_LEFT_SELECTED", &ignored) &&
           pleditSprite(skin, "PLAYLIST_TOP_LEFT_CORNER", &ignored) &&
           pleditSprite(skin, "PLAYLIST_TOP_RIGHT_CORNER_SELECTED", &ignored) &&
           pleditSprite(skin, "PLAYLIST_TOP_RIGHT_CORNER", &ignored) &&
           pleditSprite(skin, "PLAYLIST_LEFT_TILE", &ignored) &&
           pleditSprite(skin, "PLAYLIST_RIGHT_TILE", &ignored) &&
           pleditSprite(skin, "PLAYLIST_BOTTOM_TILE", &ignored) &&
           pleditSprite(skin, "PLAYLIST_BOTTOM_LEFT_CORNER", &ignored) &&
           pleditSprite(skin, "PLAYLIST_BOTTOM_RIGHT_CORNER", &ignored);
}

void tilePleditH(const Skin& skin, QPainter& painter, const SpriteRect& sprite, int x, int y,
                 int width, int height) {
    for (int left = x; left < x + width;) {
        const int step = std::min(sprite.w, x + width - left);
        skin.blitRect(painter, kSheetPledit, QRect(sprite.x, sprite.y, step, height), left, y);
        left += step;
    }
}

void tilePleditV(const Skin& skin, QPainter& painter, const QRect& src, int x, int top, int bottom) {
    for (int y = top; y < bottom;) {
        const int step = std::min(src.height(), bottom - y);
        skin.blitRect(painter, kSheetPledit, QRect(src.x(), src.y(), src.width(), step), x, y);
        y += step;
    }
}

/// The playlist's title bar: corners, the stretch tile between them, and this
/// window's name centred on the tile. The right corner keeps the skin's shade
/// and close buttons.
void drawPleditTitle(const Skin& skin, QPainter& painter, const QSize& size, const GenLayout& layout,
                     const std::string& title, bool active, const GenPalette& palette,
                     bool closePressed) {
    SpriteRect tile;
    pleditSprite(skin, active ? "PLAYLIST_TOP_TILE_SELECTED" : "PLAYLIST_TOP_TILE", &tile);

    const int corner = std::min(tile.w, size.width() / 2);
    if (size.width() > 2 * corner) {
        tilePleditH(skin, painter, tile, corner, 0, size.width() - 2 * corner, kGenTitleHeight);
    }
    skin.blit(painter, kSheetPledit, active ? "PLAYLIST_TOP_LEFT_SELECTED" : "PLAYLIST_TOP_LEFT_CORNER",
              0, 0, corner, kGenTitleHeight);
    const int rightX = size.width() - corner;
    // The corner carries the skin's shade and close buttons, the same pair the
    // playlist window ends with. Only the close box is live; the shade button
    // is the corner's own art.
    skin.blit(painter, kSheetPledit,
              active ? "PLAYLIST_TOP_RIGHT_CORNER_SELECTED" : "PLAYLIST_TOP_RIGHT_CORNER", rightX, 0,
              corner, kGenTitleHeight);

    const int textLeft = corner;
    const int textRight = rightX;
    drawBitmapTitle(skin, painter, QRect(textLeft, 0, std::max(0, textRight - textLeft), kGenTitleHeight),
                    title, palette);

    if (closePressed) {
        skin.blit(painter, kSheetPledit, "PLAYLIST_CLOSE_SELECTED", layout.closeButton.x(),
                  layout.closeButton.y());
    }
}

void drawPleditSides(const Skin& skin, QPainter& painter, const GenLayout& layout) {
    SpriteRect left;
    SpriteRect right;
    if (pleditSprite(skin, "PLAYLIST_LEFT_TILE", &left)) {
        const int width = std::min(layout.leftBorder.width(), left.w);
        tilePleditV(skin, painter, QRect(left.x, left.y, width, left.h), layout.leftBorder.x(),
                    layout.leftBorder.top(), layout.leftBorder.bottom() + 1);
    }
    if (pleditSprite(skin, "PLAYLIST_RIGHT_TILE", &right)) {
        const int width = std::min(layout.rightBorder.width(), right.w);
        const QRect src(right.x + right.w - width, right.y, width, right.h);
        tilePleditV(skin, painter, src, layout.rightBorder.right() - width + 1,
                    layout.rightBorder.top(), layout.rightBorder.bottom() + 1);
    }
}

void drawPleditBottom(const Skin& skin, QPainter& painter, const QRect& bottom) {
    SpriteRect tile;
    SpriteRect left;
    SpriteRect right;
    if (!pleditSprite(skin, "PLAYLIST_BOTTOM_TILE", &tile) ||
        !pleditSprite(skin, "PLAYLIST_BOTTOM_LEFT_CORNER", &left) ||
        !pleditSprite(skin, "PLAYLIST_BOTTOM_RIGHT_CORNER", &right)) {
        return;
    }
    const int height = std::min(kGenBottomHeight, tile.h);
    const SpriteRect edge{tile.x, tile.y + tile.h - height, tile.w, height};
    tilePleditH(skin, painter, edge, bottom.x(), bottom.y(), bottom.width(), height);

    const int join = std::min(kPleditBottomLeftJoin, left.w);
    skin.blitRect(painter, kSheetPledit,
                  QRect(left.x, left.y + left.h - height, join, height), bottom.x(), bottom.y());
    const int grip = std::min(kPleditBottomGrip, right.w);
    skin.blitRect(painter, kSheetPledit,
                  QRect(right.x + right.w - grip, right.y + right.h - height, grip, height),
                  bottom.right() - grip + 1, bottom.y());
}

}  // namespace

GenLayout genLayout(const QSize& contentSize) {
    const int w = contentSize.width();
    const int h = contentSize.height();

    GenLayout layout;
    layout.title = QRect(0, 0, w, kGenTitleHeight);
    layout.closeButton = QRect(w - kGenCloseInsetRight - kGenCloseSize, kGenCloseInsetTop,
                               kGenCloseSize, kGenCloseSize);
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
    int width = kGenWidth + stepsX * kGenSegment;
    const int height = kGenHeight + stepsY * kGenSegmentHeight;
    // Both bottom corners are 125 px. Below that they overlap and the bar shows
    // a seam, so the floor is the smallest 25 px step that fits them (269).
    if (width < kGenBottomCorner * 2) {
        const int steps = (kGenBottomCorner * 2 - kGenWidth + kGenSegment - 1) / kGenSegment;
        width = kGenWidth + steps * kGenSegment;
    }
    return QSize(width, height);
}

bool genUsesFallback(const skins::Skin& skin) {
    return skin.sheet(kSheetGen) == nullptr;
}

QImage renderGenWindow(const skins::Skin& skin, const skins::PleditColors* pledit,
                       const QSize& contentSize, const std::string& title, bool active,
                       int scale, bool closePressed) {
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
        if (pleditFrameAvailable(skin)) {
            // The same art as the playlist window: a bar the full width, the
            // name in the middle, the close button at the end.
            drawPleditTitle(skin, painter, size, layout, title, active, palette, closePressed);
            drawPleditSides(skin, painter, layout);
            drawPleditBottom(skin, painter, layout.bottom);
        } else {
            paintFallbackFrame(painter, size, layout, active, palette);
            if (closePressed) {
                painter.fillRect(layout.closeButton, QColor(palette.edge));
            }
            // A flat frame has no baked button, so the box needs an X of its own.
            painter.setPen(QColor(palette.text));
            const QRect box = layout.closeButton.adjusted(2, 2, -2, -2);
            painter.drawLine(box.topLeft(), box.bottomRight());
            painter.drawLine(box.topRight(), box.bottomLeft());
            const int clipRight = layout.closeButton.left() - kGenCloseMargin;
            drawBitmapTitle(skin, painter,
                            QRect(4, 0, std::max(0, clipRight - 4), layout.title.height()), title,
                            palette);
        }
    } else {
        drawTitleBar(skin, painter, size, layout, title, active, palette, closePressed);
        drawSideBorder(skin, painter, "GEN_MIDDLE_LEFT", "GEN_MIDDLE_LEFT_BOTTOM",
                       layout.leftBorder.x(), layout.leftBorder.width(), layout.leftBorder.top(),
                       layout.leftBorder.bottom() + 1);
        drawSideBorder(skin, painter, "GEN_MIDDLE_RIGHT", "GEN_MIDDLE_RIGHT_BOTTOM",
                       layout.rightBorder.x(), layout.rightBorder.width(), layout.rightBorder.top(),
                       layout.rightBorder.bottom() + 1);
        drawBottomBar(skin, painter, layout.bottom);
    }
    painter.end();

    if (scale > 1) {
        image = image.scaled(size * scale, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    return image;
}

}  // namespace koalaamp::ui
