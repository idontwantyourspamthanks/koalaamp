// SPDX-License-Identifier: MIT
//
// VisualizerControls — the visualizer's on-screen transport (VZ-1 task 4).
//
// Classic skins carry no visualizer buttons, so Winamp's AVS window had no
// skinned control surface to copy. This strip borrows the genex button language
// from LI-3: a row of genex faces in the bottom-right of the render, sibling to
// the GL canvas (never painted into it), with a designed fallback when the skin
// has no genex sheet.
//
// It is deliberately small: previous/next, order, lock and fullscreen. The
// window's MilkDrop keys remain the fast path; this is the discoverable one.
//
// docs/tickets/VZ-1-visualizer.md task 4; docs/spec.md §6.1.
#pragma once

#include <QWidget>

class QHBoxLayout;

namespace koalaamp::skins {
class SkinSet;
}

namespace koalaamp::ui {

class GenexButton;

class VisualizerControls : public QWidget {
    Q_OBJECT

public:
    explicit VisualizerControls(QWidget* parent = nullptr);

    void setSkin(const skins::SkinSet* skin);
    /// Integer window scale; the strip grows with the render.
    void setScale(int scale);
    int scale() const { return m_scale; }

    /// Synchronises the toggle faces with the cycler/window state. The buttons
    /// themselves only report the user's click.
    void setState(bool random, bool locked, bool fullscreen);
    bool random() const { return m_random; }
    bool locked() const { return m_locked; }
    bool fullscreen() const { return m_fullscreen; }

    QSize sizeHint() const override;

signals:
    void previousRequested();
    void nextRequested();
    void orderToggleRequested();
    void lockedToggleRequested();
    void fullscreenToggleRequested();

private:
    void applyScale();

    QHBoxLayout* m_layout = nullptr;
    GenexButton* m_previous = nullptr;
    GenexButton* m_next = nullptr;
    GenexButton* m_order = nullptr;
    GenexButton* m_lock = nullptr;
    GenexButton* m_fullscreenButton = nullptr;
    int m_scale = 1;
    bool m_random = false;
    bool m_locked = false;
    bool m_fullscreen = false;
};

} // namespace koalaamp::ui
