// SPDX-License-Identifier: MIT
//
// WindowSet — the application-level window set (spec §5.3 `WinampWindowSet`):
// the player, equalizer and playlist windows as one unit.
//
// SE-4 Part A owns the *assembly*: the app instantiates all three windows and
// routes the toggle/close seams between them here, so the EQ window (SE-5) is
// live in the binary rather than dead code. Part B adds the snap/dock/z-order
// and layout-persistence behaviour on top of the same object.
#pragma once

#include <QObject>

#include "ui/EqWindow.hpp"
#include "ui/MainWindow.hpp"
#include "ui/PlaylistWindow.hpp"
#include "ui/SkinnedWindow.hpp"

namespace koalaamp::ui {

class WindowSet : public QObject {
    Q_OBJECT

public:
    /// The three classic windows, in the set's canonical order.
    enum class WindowId { Main, Eq, Playlist };

    WindowSet(MainWindow& main, EqWindow& equalizer, PlaylistWindow& playlist,
              QObject* parent = nullptr);
    ~WindowSet() override;

    /// Wires the inter-window seams this set owns: the main window's EQ/PL
    /// toggles show/hide their windows, and each secondary window's close
    /// button hides it. Idempotent; call once after construction.
    void attach();

    /// Shows the whole set (all three windows). Winamp opens them all.
    void showAll();

    SkinnedWindow* window(WindowId id) const;
    MainWindow& mainWindow() { return m_main; }
    EqWindow& eqWindow() { return m_eq; }
    PlaylistWindow& playlistWindow() { return m_playlist; }

private:
    MainWindow& m_main;
    EqWindow& m_eq;
    PlaylistWindow& m_playlist;
    bool m_attached = false;
};

}  // namespace koalaamp::ui
