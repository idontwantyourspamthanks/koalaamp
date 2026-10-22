// SPDX-License-Identifier: MIT
//
// GenexStyle — the classic skin's `genex.bmp` widget language: the 18 colour
// swatches, the active/pressed button, the eight scroll arrows and the two
// slider strips. `gen.bmp` supplies the frame; `genex.bmp` supplies what goes
// inside it (media-library buttons, list headers and scrollbars).
//
// Geometry is the 130x75 reference atlas from the archived Winamp skinning
// tutorial (winampskins.neocities.org/twonine) and verified against the four
// 130x75 genex sheets in third_party/skins-corpus: the top button is 47x15 with
// a 4 px bevel,
// active at y=0 and pressed at y=15; the scroll arrows are 14x14 in two rows
// (up/down at y=31, left/right at y=45); the vertical sliders are 14x28 and
// the horizontal sliders 28x14. The swatch columns are y=0, x=48+2*i, i=0..17.
//
// Fallback: a skin without genex keeps the designed dark palette and the
// ordinary Qt control look; no control becomes invisible or unclickable.
//
// docs/research/skins.md §1.2; docs/tickets/LI-3-library-ui.md.
#pragma once

#include <QColor>
#include <QPushButton>
#include <QScrollBar>
#include <QString>

class QPainter;
class QRect;
class QTimer;

namespace koalaamp::skins {
class Skin;
}

namespace koalaamp::ui {

/// The 18 colours `genex.bmp` carries. `valid` is false when the skin has no
/// usable genex sheet; the remaining values are then the designed fallback.
struct GenexPalette {
    bool valid = false;
    QColor itemBackground;
    QColor itemText;
    QColor windowBackground;
    QColor buttonText;
    QColor windowText;
    QColor divider;
    QColor selection;
    QColor headerBackground;
    QColor headerText;
    QColor headerTopLeft;
    QColor headerBottomRight;
    QColor headerPressed;
    QColor headerDead;
    QColor scrollbar1;
    QColor scrollbar2;
    QColor pressedScrollbar1;
    QColor pressedScrollbar2;
    QColor scrollbarDead;

    bool operator==(const GenexPalette&) const = default;
};

/// Samples the 18 swatches from `genex.bmp`; missing/unreadable columns fall
/// back individually, so a partially painted sheet still yields a usable theme.
GenexPalette genexPalette(const skins::Skin* skin);

/// True when the skin carries a genex sheet with at least one usable sprite.
bool genexHasArt(const skins::Skin* skin);

/// Paints the active or pressed button face (a 4 px 9-slice) and its centred
/// text. Returns false when the sheet/sprite is absent, so the caller can fall
/// back to its ordinary style.
bool paintGenexButton(QPainter& painter, const skins::Skin* skin, const QRect& rect,
                      const QString& text, bool pressed, int textPixelSize = 11);

/// A QPushButton that paints the genex face when the current skin has one.
class GenexButton final : public QPushButton {
    Q_OBJECT

public:
    explicit GenexButton(const QString& text, QWidget* parent = nullptr);
    void setSkin(const skins::Skin* skin);
    const skins::Skin* skin() const { return m_skin; }
    /// Pixel height of the label; the control strip scales it with the window.
    void setTextPixelSize(int pixels);
    int textPixelSize() const { return m_textPixelSize; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const skins::Skin* m_skin = nullptr;
    int m_textPixelSize = 11;
};

/// A QScrollBar that paints the genex arrows/slider (14 px thick) while keeping
/// QScrollBar's keyboard, wheel and drag behaviour. A skin without genex falls
/// back to the normal style.
class GenexScrollBar final : public QScrollBar {
public:
    explicit GenexScrollBar(Qt::Orientation orientation, QWidget* parent = nullptr);
    void setSkin(const skins::Skin* skin);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class Arrow { None, Start, End };
    QRect startArrowRect() const;
    QRect endArrowRect() const;
    Arrow arrowAt(const QPoint& point) const;
    void stepArrow(Arrow arrow);

    const skins::Skin* m_skin = nullptr;
    GenexPalette m_palette;
    Arrow m_pressedArrow = Arrow::None;
    QTimer* m_repeatTimer = nullptr;
};

} // namespace koalaamp::ui
