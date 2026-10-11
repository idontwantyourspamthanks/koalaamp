// SPDX-License-Identifier: MIT
#include "ui/SkinnedWindow.hpp"

#include <QMouseEvent>
#include <QRegion>

#include "ui/SkinHitRegion.hpp"

namespace koalaamp::ui {

SkinnedWindow::SkinnedWindow(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_NoSystemBackground, true);
}

void SkinnedWindow::setWindowShape(const skins::Region& region, bool shade,
                                   const QSize& contentSize, int scale) {
    // region.txt coordinates are 1x skin pixels; scale them for hi-dpi skins.
    skins::Region scaledRegion;
    if (scale > 1) {
        for (const auto& [section, polygons] : region.sections) {
            auto& target = scaledRegion.sections[section];
            for (const auto& polygon : polygons) {
                skins::RegionPolygon scaledPolygon;
                for (const auto& point : polygon.points) {
                    scaledPolygon.points.push_back({point.x * scale, point.y * scale});
                }
                target.push_back(std::move(scaledPolygon));
            }
        }
    }
    const skins::Region& source = scale > 1 ? scaledRegion : region;

    const QRegion mask = buildWindowMask(source, shade, contentSize);
    if (mask.isEmpty()) {
        clearMask();  // no polygons for this state: rectangular, like Winamp
    } else {
        setMask(mask);
    }
    setFixedSize(contentSize);
}

void SkinnedWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && isTitleBarAt(event->pos())) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void SkinnedWindow::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging && (event->buttons() & Qt::LeftButton) != 0) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void SkinnedWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        emit dragFinished();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SkinnedWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && isTitleBarAt(event->pos())) {
        emit shadeToggleRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

}  // namespace koalaamp::ui
