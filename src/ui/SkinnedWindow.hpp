// SPDX-License-Identifier: MIT
//
// SkinnedWindow — the base of every classic-skinned toplevel: a frameless
// QWidget whose shape comes from the skin's `region.txt` polygons, with
// title-bar dragging and the double-click shade toggle Winamp has.
//
// Shapes use `QWidget::setMask(QRegion)`, which is `SetWindowRgn` on Windows
// and XShape on X11 — both clip output *and* input, no compositor needed
// (docs/spec.md §5.2-5.3; mechanics proven by spikes/fe1). On backends whose
// mask does not clip input (macOS CALayer) the same polygons answer
// `koalaamp::ui::maskContains` for the app-level hit test.
#pragma once

#include <QPoint>
#include <QWidget>

#include "skins/SkinText.hpp"

namespace koalaamp::ui {

class SkinnedWindow : public QWidget {
    Q_OBJECT

public:
    explicit SkinnedWindow(QWidget* parent = nullptr);

    /// Applies the skin shape (empty region == rectangular) and resizes the
    /// window. `scale` multiplies the 1x `region.txt` coordinates.
    void setWindowShape(const skins::Region& region, bool shade, const QSize& contentSize,
                        int scale = 1);

    /// True while the window is being dragged by its title bar.
    bool isDragging() const { return m_dragging; }

    /// Integer content scale. The base does nothing: a window that is held
    /// type-erased (the visualizer's slot in `WindowSet`) may not care, and the
    /// classic windows override it.
    virtual void setScale(int scale) { (void)scale; }

signals:
    /// Emitted once when a title-bar drag ends (the snapping hook, SE-4).
    void dragFinished();
    /// Title-bar double click (Winamp toggles windowshade).
    void shadeToggleRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

    /// Does this window-local point (device pixels) hit a draggable title bar?
    virtual bool isTitleBarAt(const QPoint& point) const = 0;

private:
    bool m_dragging = false;
    QPoint m_dragOffset;
};

}  // namespace koalaamp::ui
