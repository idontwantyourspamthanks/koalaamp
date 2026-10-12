// SPDX-License-Identifier: MIT
#include "ui/PleditCanvas.hpp"

#include <QPainter>

#include "ui/PleditRenderer.hpp"

namespace koalaamp::ui {

PleditCanvas::PleditCanvas(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
}

void PleditCanvas::setSkin(const skins::Skin* sheets, const skins::PleditColors& colors) {
    m_sheets = sheets;
    m_colors = colors;
    recompose();
    update();
}

void PleditCanvas::setState(const PleditState& state) {
    m_state = state;
    recompose();
    update();
}

void PleditCanvas::setScale(int scale) {
    const int clamped = scale < 1 ? 1 : scale;
    if (clamped == m_scale) {
        return;
    }
    m_scale = clamped;
    setFixedSize(contentSize());
    update();
}

QSize PleditCanvas::contentSize() const {
    const int height = m_state.shade ? kPleditShadeHeight : pleditHeight(m_state);
    return QSize(pleditWidth(m_state) * m_scale, height * m_scale);
}

void PleditCanvas::recompose() {
    const int height = m_state.shade ? kPleditShadeHeight : pleditHeight(m_state);
    const int width = pleditWidth(m_state);
    if (m_sheets == nullptr) {
        m_frame = QImage(width, height, QImage::Format_RGB32);
        m_frame.fill(QColor(m_colors.normalBg.r, m_colors.normalBg.g, m_colors.normalBg.b));
        return;
    }
    m_frame = renderPleditWindow(*m_sheets, m_colors, m_state, 1);
}

void PleditCanvas::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (m_frame.isNull()) {
        return;
    }
    if (m_scale <= 1) {
        painter.drawImage(0, 0, m_frame);
    } else {
        painter.drawImage(rect(), m_frame);  // nearest neighbour integer upscale
    }
}

}  // namespace koalaamp::ui
