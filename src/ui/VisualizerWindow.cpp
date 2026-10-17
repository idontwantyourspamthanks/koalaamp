// SPDX-License-Identifier: MIT
//
// VisualizerWindow — see VisualizerWindow.hpp for the hosting and fallback rules.
#include "ui/VisualizerWindow.hpp"

#include <algorithm>

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QMouseEvent>
#include <QPainter>

namespace koalaamp::ui {
namespace {

/// An empty sheet set: renderGenWindow then paints the designed fallback frame,
/// which is what this window shows before a skin is chosen (the main and EQ
/// windows paint black instead — a gen window is meant to stay usable).
const skins::Skin& noSheets() {
    static const skins::Skin empty;
    return empty;
}

QImage chromeImage(const skins::Skin& sheets, const QSize& contentSize, const std::string& title,
                   bool active, int scale) {
    return renderGenWindow(sheets, contentSize, title, active, scale);
}

}  // namespace

VisualizerWindow::VisualizerWindow(interop::visualizer::IVisualizerEngine& engine, QWidget* parent)
    : SkinnedWindow(parent), m_engine(engine) {
    m_canvas = new VisualizerCanvas(m_engine, this);
    m_canvas->setVisible(visualizerAvailable());
    applyTitle();
    updateShape();
}

VisualizerWindow::~VisualizerWindow() = default;

void VisualizerWindow::setSkin(const skins::SkinSet* skin) {
    m_skin = skin;
    updateShape();
}

bool VisualizerWindow::usesFallbackChrome() const {
    return m_skin == nullptr || genUsesFallback(m_skin->sheets);
}

bool VisualizerWindow::visualizerAvailable() const {
    return m_engine.available();
}

void VisualizerWindow::setScale(int scale) {
    const int clamped = std::clamp(scale, 1, 4);
    if (clamped == m_scale) {
        return;
    }
    m_scale = clamped;
    updateShape();
    emit doubleSizeChanged(m_scale > 1);
}

void VisualizerWindow::setDoubleSize(bool on) {
    setScale(on ? 2 : 1);
}

void VisualizerWindow::setAlwaysOnTop(bool on) {
    const QPoint position = pos();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    show();
    move(position);
    updateShape();
    emit alwaysOnTopChanged(on);
}

bool VisualizerWindow::isAlwaysOnTop() const {
    return (windowFlags() & Qt::WindowStaysOnTopHint) != 0;
}

void VisualizerWindow::setTitle(const std::string& title) {
    m_title = title;
    applyTitle();
    update();
}

void VisualizerWindow::setClientSize(const QSize& clientSize) {
    const QSize snapped = genSnapSize(clientSize);
    if (snapped == m_clientSize) {
        return;
    }
    m_clientSize = snapped;
    updateShape();
}

void VisualizerWindow::updateShape() {
    // A gen window has no `region.txt` section: its frame is rectangular, so the
    // mask is cleared rather than built (FE-14's masked-toplevel result still
    // covers any skin that ever shapes one).
    static const skins::Region none;
    setWindowShape(none, false, contentSize() * m_scale, m_scale);
    layoutCanvas();
    update();
}

void VisualizerWindow::layoutCanvas() {
    if (m_canvas == nullptr) {
        return;
    }
    m_canvas->setGeometry(toDevice(clientRect()));
    m_canvas->setVisible(visualizerAvailable());
}

void VisualizerWindow::applyTitle() {
    setWindowTitle(QString::fromStdString(m_title) + QStringLiteral(" - KoalaAmp"));
}

void VisualizerWindow::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const skins::Skin& sheets = m_skin != nullptr ? m_skin->sheets : noSheets();
    painter.drawImage(0, 0, chromeImage(sheets, contentSize(), m_title, isActiveWindow(), m_scale));

    if (!visualizerAvailable()) {
        drawUnavailableMessage(painter);
    }
}

void VisualizerWindow::drawUnavailableMessage(QPainter& painter) {
    QFont font;
    font.setPixelSize(std::max(8, 6 * m_scale));
    painter.setFont(font);
    painter.setPen(QColor(0xB0B0C0));
    painter.drawText(toDevice(clientRect()), Qt::AlignCenter,
                     QStringLiteral("VISUALIZER UNAVAILABLE\nTHIS BUILD HAS NO LIBPROJECTM"));
}

QRect VisualizerWindow::toDevice(const QRect& rect) const {
    return QRect(rect.x() * m_scale, rect.y() * m_scale, rect.width() * m_scale,
                 rect.height() * m_scale);
}

bool VisualizerWindow::isTitleBarAt(const QPoint& point) const {
    const GenLayout layout = genLayout(contentSize());
    // The close box is part of the title bar visually, but dragging from it must
    // not move the window.
    return toDevice(layout.title).contains(point) &&
           !toDevice(layout.closeButton).contains(point);
}

void VisualizerWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && toDevice(closeButtonRect()).contains(event->pos())) {
        m_closePressed = true;
        event->accept();
        return;
    }
    SkinnedWindow::mousePressEvent(event);
}

void VisualizerWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (m_closePressed) {
        const bool inside = toDevice(closeButtonRect()).contains(event->pos());
        m_closePressed = false;
        if (inside) {
            emit closeRequested();
        }
        event->accept();
        return;
    }
    SkinnedWindow::mouseReleaseEvent(event);
}

void VisualizerWindow::changeEvent(QEvent* event) {
    SkinnedWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        update();  // the skin has distinct selected/unselected title sprites
    }
}

}  // namespace koalaamp::ui
