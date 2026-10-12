// SPDX-License-Identifier: MIT
#include "ui/WindowSet.hpp"

namespace koalaamp::ui {

WindowSet::WindowSet(MainWindow& main, EqWindow& equalizer, PlaylistWindow& playlist,
                     QObject* parent)
    : QObject(parent), m_main(main), m_eq(equalizer), m_playlist(playlist) {}

WindowSet::~WindowSet() = default;

void WindowSet::attach() {
    if (m_attached) {
        return;
    }
    m_attached = true;

    // Main window toggles (the SE-3 playlist pattern, now shared with the EQ).
    QObject::connect(&m_main, &MainWindow::eqToggled, &m_eq,
                     [this](bool on) { on ? m_eq.show() : m_eq.hide(); });
    QObject::connect(&m_main, &MainWindow::playlistToggled, &m_playlist,
                     [this](bool on) { on ? m_playlist.show() : m_playlist.hide(); });

    // Each secondary window's own close button hides it (Winamp behaviour).
    QObject::connect(&m_eq, &EqWindow::closeRequested, &m_eq, &QWidget::hide);
    QObject::connect(&m_playlist, &PlaylistWindow::closeRequested, &m_playlist, &QWidget::hide);
}

void WindowSet::showAll() {
    m_main.show();
    m_eq.show();
    m_playlist.show();
}

SkinnedWindow* WindowSet::window(WindowId id) const {
    switch (id) {
        case WindowId::Main:
            return &m_main;
        case WindowId::Eq:
            return &m_eq;
        case WindowId::Playlist:
            return &m_playlist;
    }
    return nullptr;
}

}  // namespace koalaamp::ui
