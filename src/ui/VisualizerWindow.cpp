// SPDX-License-Identifier: MIT
//
// VisualizerWindow — see VisualizerWindow.hpp for the hosting and fallback rules.
#include "ui/VisualizerWindow.hpp"

#include "ui/PresetStore.hpp"

#include <algorithm>

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
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
    // The overlay is a sibling above the canvas: the canvas owns the GL path, and
    // painting text into it would mean mixing Qt painting with projectM's GL state.
    m_overlay = new VisualizerOverlay(this);
    m_overlay->hide();
    applyTitle();
    updateShape();
}

VisualizerWindow::~VisualizerWindow() = default;

void VisualizerWindow::setSkin(const skins::SkinSet* skin) {
    m_skin = skin;
    if (m_overlay != nullptr) {
        m_overlay->setSkin(skin);
    }
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
    if (m_fullscreen) {
        // Fullscreen owns the geometry: setWindowShape() pins a fixed size, so a
        // skin load, a scale change or a client-size request arriving while
        // fullscreen would pull the window back out of it. The shape is re-applied
        // by setFullscreen(false), which calls this again.
        layoutCanvas();
        update();
        return;
    }
    // A gen window has no `region.txt` section: its frame is rectangular, so the
    // mask is cleared rather than built (FE-14's masked-toplevel result still
    // covers any skin that ever shapes one).
    static const skins::Region none;
    setWindowShape(none, false, contentSize() * m_scale, m_scale);
    layoutCanvas();
    update();
}

void VisualizerWindow::setFullscreen(bool on) {
    if (on == m_fullscreen) {
        return;
    }
    m_fullscreen = on;
    if (on) {
        m_windowedGeometry = geometry();
        // The windowed shape pins a fixed size; fullscreen has to be free to take
        // the whole screen. updateShape() restores it on the way out.
        setMinimumSize(0, 0);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        showFullScreen();
        layoutCanvas();
        raise();
        activateWindow();
    } else {
        showNormal();
        updateShape();  // fixed size and shape again
        if (m_windowedGeometry.isValid()) {
            setGeometry(m_windowedGeometry);
        }
    }
    layoutCanvas();
    update();
    emit fullscreenChanged(on);
}

void VisualizerWindow::layoutCanvas() {
    if (m_canvas == nullptr) {
        return;
    }
    // Fullscreen shows the render alone, so the canvas takes the whole window
    // instead of the gen frame's client rect.
    const QRect client = m_fullscreen ? rect() : toDevice(clientRect());
    m_canvas->setGeometry(client);
    m_canvas->setVisible(visualizerAvailable());
    if (m_overlay != nullptr) {
        m_overlay->setGeometry(client);
        m_overlay->setScale(m_scale);
        m_overlay->raise();  // stays above the canvas it annotates
    }
}

void VisualizerWindow::showPresetName(const std::string& path) {
    m_presetName = path.empty() ? std::string{} : PresetStore::presetDisplayName(path);
    m_overlay->showName(m_presetName);
}

bool VisualizerWindow::isPresetNameVisible() const {
    return m_overlay != nullptr && m_overlay->showing();
}

qreal VisualizerWindow::presetNameOpacity() const {
    return m_overlay != nullptr ? m_overlay->opacity() : 0.0;
}

void VisualizerWindow::keyPressEvent(QKeyEvent* event) {
    // MilkDrop's own keys, on the visualizer window: N/P move, R toggles random
    // order, L locks the preset in place. The window only asks; the app decides.
    // (F for fullscreen is task 6, and app-wide hotkeys are a later ticket.)
    switch (event->key()) {
        case Qt::Key_N:
            emit nextPresetRequested();
            break;
        case Qt::Key_P:
            emit previousPresetRequested();
            break;
        case Qt::Key_R:
            emit orderToggleRequested();
            break;
        case Qt::Key_L:
            emit lockedToggleRequested();
            break;
        case Qt::Key_F:
            setFullscreen(!m_fullscreen);
            break;
        case Qt::Key_Escape:
            if (!m_fullscreen) {
                SkinnedWindow::keyPressEvent(event);
                return;
            }
            setFullscreen(false);
            break;
        default:
            SkinnedWindow::keyPressEvent(event);
            return;
    }
    event->accept();
}

void VisualizerWindow::applyTitle() {
    setWindowTitle(QString::fromStdString(m_title) + QStringLiteral(" - KoalaAmp"));
}

void VisualizerWindow::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (m_fullscreen) {
        // The canvas covers the window; this is only the frame before it appears.
        painter.fillRect(rect(), Qt::black);
        if (!visualizerAvailable()) {
            drawUnavailableMessage(painter);
        }
        return;
    }

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

void VisualizerWindow::resizeEvent(QResizeEvent* event) {
    SkinnedWindow::resizeEvent(event);
    layoutCanvas();
}

void VisualizerWindow::changeEvent(QEvent* event) {
    SkinnedWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        update();  // the skin has distinct selected/unselected title sprites
    }
}

}  // namespace koalaamp::ui
