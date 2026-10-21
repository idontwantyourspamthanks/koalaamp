// SPDX-License-Identifier: MIT
// GenexStyle — see the header for the atlas geometry and fallback contract.

#include "ui/GenexStyle.hpp"

#include "skins/Skin.hpp"
#include "skins/SkinSprites.hpp"

#include <QFont>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionSlider>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

namespace koalaamp::ui {
namespace {

constexpr char kSheetGenex[] = "GENEX";
constexpr int kScrollExtent = 14;
constexpr int kButtonBorder = 4;
constexpr int kSwatchCount = 18;

QColor swatch(const skins::Skin& skin, int index) {
    const QImage* sheet = skin.sheet(kSheetGenex);
    if (sheet == nullptr || sheet->isNull() || sheet->height() < 1) {
        return {};
    }
    const int x = 48 + index * 2;
    if (x < 0 || x >= sheet->width()) {
        return {};
    }
    const QRgb pixel = sheet->pixel(x, 0);
    // RGB(0,198,255) is the atlas' transparent marker, not a colour a skin
    // should use for text; treat it as an absent swatch.
    if (qRed(pixel) == 0 && qGreen(pixel) == 198 && qBlue(pixel) == 255) {
        return {};
    }
    return QColor::fromRgb(pixel);
}

/// Blits a 4 px-bordered sprite into `dest`, stretching the centre and edges;
/// used for the active/pressed genex button face.
void drawNineSlice(QPainter& painter, const QImage& sheet, const QRect& src, const QRect& dest,
                   int border) {
    if (src.isEmpty() || dest.isEmpty()) {
        return;
    }
    if (dest.width() < border * 2 || dest.height() < border * 2) {
        painter.drawImage(dest, sheet, src);
        return;
    }
    const int sourceMiddleW = src.width() - border * 2;
    const int sourceMiddleH = src.height() - border * 2;
    const int destMiddleW = dest.width() - border * 2;
    const int destMiddleH = dest.height() - border * 2;
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    // Corners.
    painter.drawImage(QRect(dest.left(), dest.top(), border, border), sheet,
                      QRect(src.left(), src.top(), border, border));
    painter.drawImage(QRect(dest.right() - border + 1, dest.top(), border, border), sheet,
                      QRect(src.right() - border + 1, src.top(), border, border));
    painter.drawImage(QRect(dest.left(), dest.bottom() - border + 1, border, border), sheet,
                      QRect(src.left(), src.bottom() - border + 1, border, border));
    painter.drawImage(QRect(dest.right() - border + 1, dest.bottom() - border + 1, border, border),
                      sheet,
                      QRect(src.right() - border + 1, src.bottom() - border + 1, border, border));
    // Edges.
    painter.drawImage(QRect(dest.left() + border, dest.top(), destMiddleW, border), sheet,
                      QRect(src.left() + border, src.top(), sourceMiddleW, border));
    painter.drawImage(QRect(dest.left() + border, dest.bottom() - border + 1, destMiddleW, border),
                      sheet,
                      QRect(src.left() + border, src.bottom() - border + 1, sourceMiddleW, border));
    painter.drawImage(QRect(dest.left(), dest.top() + border, border, destMiddleH), sheet,
                      QRect(src.left(), src.top() + border, border, sourceMiddleH));
    painter.drawImage(QRect(dest.right() - border + 1, dest.top() + border, border, destMiddleH),
                      sheet,
                      QRect(src.right() - border + 1, src.top() + border, border, sourceMiddleH));
    // Centre.
    painter.drawImage(QRect(dest.left() + border, dest.top() + border, destMiddleW, destMiddleH),
                      sheet,
                      QRect(src.left() + border, src.top() + border, sourceMiddleW, sourceMiddleH));
    painter.restore();
}

void drawButtonText(QPainter& painter, const QString& text, const QRect& area,
                    const QColor& colour) {
    // genex has no bitmap alphabet of its own; the controls use the skin's
    // system font, recoloured by swatch 3 (button text). The gen.bmp alphabet
    // is the title-bar font only.
    QFont font = painter.font();
    font.setPixelSize(11);
    painter.save();
    painter.setFont(font);
    painter.setPen(colour);
    painter.drawText(area, Qt::AlignCenter, text);
    painter.restore();
}

QRect spriteRect(const skins::Skin& skin, const char* name) {
    const auto rect = skin.spriteRect(kSheetGenex, name);
    return rect ? QRect(rect->x, rect->y, rect->w, rect->h) : QRect();
}

} // namespace

GenexPalette genexPalette(const skins::Skin* skin) {
    GenexPalette palette;
    palette.itemBackground = QColor(0x0e, 0x12, 0x16);
    palette.itemText = QColor(0xe8, 0xe8, 0xe8);
    palette.windowBackground = QColor(0x10, 0x14, 0x18);
    palette.buttonText = QColor(0xe8, 0xe8, 0xe8);
    palette.windowText = QColor(0xe8, 0xe8, 0xe8);
    palette.divider = QColor(0x50, 0x50, 0x50);
    palette.selection = QColor(0x1e, 0x4f, 0x68);
    palette.headerBackground = QColor(0x1a, 0x20, 0x28);
    palette.headerText = QColor(0xe8, 0xe8, 0xe8);
    palette.headerTopLeft = QColor(0x60, 0x60, 0x60);
    palette.headerBottomRight = QColor(0x30, 0x30, 0x30);
    palette.headerPressed = QColor(0x24, 0x2c, 0x34);
    palette.headerDead = QColor(0x18, 0x1e, 0x24);
    palette.scrollbar1 = QColor(0x1a, 0x20, 0x28);
    palette.scrollbar2 = QColor(0x3a, 0x4a, 0x58);
    palette.pressedScrollbar1 = QColor(0x2a, 0x3a, 0x48);
    palette.pressedScrollbar2 = QColor(0x50, 0x68, 0x78);
    palette.scrollbarDead = QColor(0x14, 0x1a, 0x20);
    if (skin == nullptr) {
        return palette;
    }

    const std::array<QColor*, kSwatchCount> fields = {
        &palette.itemBackground,    &palette.itemText,          &palette.windowBackground,
        &palette.buttonText,        &palette.windowText,        &palette.divider,
        &palette.selection,         &palette.headerBackground,  &palette.headerText,
        &palette.headerTopLeft,     &palette.headerBottomRight, &palette.headerPressed,
        &palette.headerDead,        &palette.scrollbar1,        &palette.scrollbar2,
        &palette.pressedScrollbar1, &palette.pressedScrollbar2, &palette.scrollbarDead,
    };
    for (int i = 0; i < kSwatchCount; ++i) {
        if (const QColor colour = swatch(*skin, i); colour.isValid()) {
            *fields[static_cast<std::size_t>(i)] = colour;
            palette.valid = true;
        }
    }
    return palette;
}

bool genexHasArt(const skins::Skin* skin) {
    return skin != nullptr && !spriteRect(*skin, "GENEX_SCROLL_UP").isEmpty();
}

bool paintGenexButton(QPainter& painter, const skins::Skin* skin, const QRect& rect,
                      const QString& text, bool pressed) {
    if (skin == nullptr) {
        return false;
    }
    const QImage* sheet = skin->sheet(kSheetGenex);
    if (sheet == nullptr || sheet->isNull()) {
        return false;
    }
    const QRect src = spriteRect(*skin, pressed ? "GENEX_BUTTON_PRESSED" : "GENEX_BUTTON_ACTIVE");
    if (src.isEmpty() || !sheet->rect().contains(src)) {
        return false;
    }
    drawNineSlice(painter, *sheet, src, rect, kButtonBorder);
    const GenexPalette palette = genexPalette(skin);
    drawButtonText(painter, text, rect, palette.buttonText);
    return true;
}

GenexButton::GenexButton(const QString& text, QWidget* parent) : QPushButton(text, parent) {
    setFlat(true);
    setCursor(Qt::PointingHandCursor);
}

void GenexButton::setSkin(const skins::Skin* skin) {
    if (m_skin == skin) {
        return;
    }
    m_skin = skin;
    update();
}

void GenexButton::paintEvent(QPaintEvent* event) {
    if (m_skin == nullptr) {
        QPushButton::paintEvent(event);
        return;
    }
    QPainter painter(this);
    if (paintGenexButton(painter, m_skin, rect(), text(), isDown())) {
        return;
    }
    QPushButton::paintEvent(event);
}

GenexScrollBar::GenexScrollBar(Qt::Orientation orientation, QWidget* parent)
    : QScrollBar(orientation, parent) {
    setObjectName(orientation == Qt::Vertical ? QStringLiteral("genexVerticalScrollBar")
                                              : QStringLiteral("genexHorizontalScrollBar"));
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    if (orientation == Qt::Vertical) {
        setFixedWidth(kScrollExtent);
    } else {
        setFixedHeight(kScrollExtent);
    }
    m_repeatTimer = new QTimer(this);
    m_repeatTimer->setInterval(400);
    connect(m_repeatTimer, &QTimer::timeout, this, [this] {
        if (m_pressedArrow == Arrow::None) {
            m_repeatTimer->stop();
            return;
        }
        stepArrow(m_pressedArrow);
        if (m_repeatTimer->interval() != 50) {
            m_repeatTimer->setInterval(50);
        }
    });
}

void GenexScrollBar::setSkin(const skins::Skin* skin) {
    m_skin = skin;
    m_palette = genexPalette(skin);
    m_pressedArrow = Arrow::None;
    update();
}

QRect GenexScrollBar::startArrowRect() const {
    return QRect(0, 0, kScrollExtent, kScrollExtent);
}

QRect GenexScrollBar::endArrowRect() const {
    return orientation() == Qt::Vertical
               ? QRect(0, height() - kScrollExtent, kScrollExtent, kScrollExtent)
               : QRect(width() - kScrollExtent, 0, kScrollExtent, kScrollExtent);
}

GenexScrollBar::Arrow GenexScrollBar::arrowAt(const QPoint& point) const {
    if (startArrowRect().contains(point)) {
        return Arrow::Start;
    }
    if (endArrowRect().contains(point)) {
        return Arrow::End;
    }
    return Arrow::None;
}

void GenexScrollBar::stepArrow(Arrow arrow) {
    const int delta = arrow == Arrow::End ? singleStep() : -singleStep();
    setValue(std::clamp(value() + delta, minimum(), maximum()));
}

void GenexScrollBar::mousePressEvent(QMouseEvent* event) {
    const Arrow arrow = arrowAt(event->pos());
    if (arrow != Arrow::None && event->button() == Qt::LeftButton) {
        m_pressedArrow = arrow;
        stepArrow(arrow);
        m_repeatTimer->start();
        update();
        event->accept();
        return;
    }
    m_pressedArrow = Arrow::None;
    QScrollBar::mousePressEvent(event);
    update();
}

void GenexScrollBar::mouseReleaseEvent(QMouseEvent* event) {
    if (m_pressedArrow != Arrow::None) {
        m_repeatTimer->stop();
        m_pressedArrow = Arrow::None;
        update();
        event->accept();
        return;
    }
    QScrollBar::mouseReleaseEvent(event);
}

void GenexScrollBar::paintEvent(QPaintEvent* event) {
    if (!genexHasArt(m_skin)) {
        QScrollBar::paintEvent(event);
        return;
    }
    const bool vertical = orientation() == Qt::Vertical;
    const int length = vertical ? height() : width();
    if (length <= kScrollExtent * 2) {
        QScrollBar::paintEvent(event);
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const QImage* sheet = m_skin->sheet(kSheetGenex);
    const QColor groove = isEnabled() ? m_palette.scrollbar1 : m_palette.scrollbarDead;
    const QColor slider =
        isEnabled() ? (isSliderDown() ? m_palette.pressedScrollbar2 : m_palette.scrollbar2)
                    : m_palette.scrollbarDead;
    painter.fillRect(rect(), groove);

    const int trackStart = kScrollExtent;
    const int trackLength = length - kScrollExtent * 2;
    const int span = maximum() - minimum();
    // genex scrollbars use a fixed-size slider (the 28 px long axis of the
    // sprite), not a proportional thumb: Winamp's classic scrollbars slide a
    // fixed bitmap through the groove. Only shrink it when the groove is too
    // short to hold the sprite.
    const int sliderLength = std::min(28, trackLength);
    int sliderOffset = 0;
    if (span > 0) {
        sliderOffset = static_cast<int>(static_cast<qint64>(sliderPosition() - minimum()) *
                                        (trackLength - sliderLength) / span);
    }
    const QRect sliderRect = vertical
                                 ? QRect(0, trackStart + sliderOffset, kScrollExtent, sliderLength)
                                 : QRect(trackStart + sliderOffset, 0, sliderLength, kScrollExtent);

    // Arrows: the atlas has pressed variants for each direction. A missing
    // sprite falls back to the swatch colour so the control stays legible.
    const auto drawArrow = [&](const QRect& dest, bool end, bool pressed) {
        if (sheet != nullptr) {
            const char* name =
                vertical ? (end ? (pressed ? "GENEX_SCROLL_DOWN_PRESSED" : "GENEX_SCROLL_DOWN")
                                : (pressed ? "GENEX_SCROLL_UP_PRESSED" : "GENEX_SCROLL_UP"))
                         : (end ? (pressed ? "GENEX_SCROLL_RIGHT_PRESSED" : "GENEX_SCROLL_RIGHT")
                                : (pressed ? "GENEX_SCROLL_LEFT_PRESSED" : "GENEX_SCROLL_LEFT"));
            if (const auto src = m_skin->spriteRect(kSheetGenex, name)) {
                painter.drawImage(dest, *sheet, QRect(src->x, src->y, src->w, src->h));
                return;
            }
        }
        painter.fillRect(dest, groove);
        painter.setPen(m_palette.divider);
        const QPoint c = dest.center();
        if (vertical) {
            const int h = end ? 5 : -5;
            painter.drawLine(c + QPoint(-3, -h / 2), c + QPoint(3, -h / 2));
            painter.drawLine(c + QPoint(3, -h / 2), c + QPoint(0, h / 2));
            painter.drawLine(c + QPoint(0, h / 2), c + QPoint(-3, -h / 2));
        } else {
            const int h = end ? 5 : -5;
            painter.drawLine(c + QPoint(-h / 2, -3), c + QPoint(-h / 2, 3));
            painter.drawLine(c + QPoint(-h / 2, 3), c + QPoint(h / 2, 0));
            painter.drawLine(c + QPoint(h / 2, 0), c + QPoint(-h / 2, -3));
        }
    };

    drawArrow(startArrowRect(), false, m_pressedArrow == Arrow::Start);
    drawArrow(endArrowRect(), true, m_pressedArrow == Arrow::End);

    // Slider face. The 14x28 / 28x14 sprite is stretched along the long axis.
    if (sheet != nullptr) {
        const char* name = vertical ? (isSliderDown() ? "GENEX_VSLIDER_PRESSED" : "GENEX_VSLIDER")
                                    : (isSliderDown() ? "GENEX_HSLIDER_PRESSED" : "GENEX_HSLIDER");
        if (const auto src = m_skin->spriteRect(kSheetGenex, name)) {
            painter.drawImage(sliderRect, *sheet, QRect(src->x, src->y, src->w, src->h));
            return;
        }
    }
    painter.fillRect(sliderRect, slider);
    painter.setPen(m_palette.pressedScrollbar1);
    painter.drawRect(sliderRect.adjusted(0, 0, -1, -1));
}

} // namespace koalaamp::ui
