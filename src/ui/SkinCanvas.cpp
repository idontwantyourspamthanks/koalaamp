// SPDX-License-Identifier: MIT
#include "ui/SkinCanvas.hpp"

#include <QPainter>

#include "ui/PlayerRenderer.hpp"
#include "ui/TitleText.hpp"

namespace koalaamp::ui {

SkinCanvas::SkinCanvas(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
}

void SkinCanvas::setSkin(const skins::Skin* sheets, const skins::VisColor& visColor) {
    m_sheets = sheets;
    m_vis = visColor;
    invalidateChrome();
    update();
}

void SkinCanvas::setState(const PlayerState& state) {
    if (chromeKey(state) != m_chromeKey) {
        m_chromeValid = false;
    }
    m_state = state;
    update();
}

void SkinCanvas::setScale(int scale) {
    const int clamped = scale < 1 ? 1 : scale;
    if (clamped == m_scale) {
        return;
    }
    m_scale = clamped;
    invalidateChrome();
    setFixedSize(contentSize());
    update();
}

QSize SkinCanvas::contentSize() const {
    const int height = m_state.shade ? kShadeHeight : kMainHeight;
    return QSize(kMainWidth * m_scale, height * m_scale);
}

void SkinCanvas::invalidateChrome() {
    m_chromeValid = false;
    m_chromeKey = 0;
}

void SkinCanvas::ensureChrome() {
    const std::uint64_t key = chromeKey(m_state);
    if (m_chromeValid && key == m_chromeKey && m_chrome.size() == QSize(kMainWidth,
                                                                       m_state.shade
                                                                           ? kShadeHeight
                                                                           : kMainHeight)) {
        return;
    }
    if (m_sheets == nullptr) {
        m_chrome = QImage(kMainWidth, m_state.shade ? kShadeHeight : kMainHeight,
                          QImage::Format_RGB32);
        m_chrome.fill(Qt::black);
    } else {
        m_chrome = renderChrome(*m_sheets, m_vis, m_state, 1);
    }
    m_chromeKey = key;
    m_chromeValid = true;
}

void SkinCanvas::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    ensureChrome();

    if (m_scale <= 1) {
        painter.drawImage(0, 0, m_chrome);
    } else {
        // Integer upscale of the 1x composition: nearest neighbour only.
        painter.drawImage(rect(), m_chrome);
    }

    if (m_sheets != nullptr && !m_state.shade) {
        drawVisualizerOverlay(painter, m_vis, m_state, m_scale);
    }
}

}  // namespace koalaamp::ui
