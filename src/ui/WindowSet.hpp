// SPDX-License-Identifier: MIT
//
// WindowSet — the application-level window set (spec §5.3 `WinampWindowSet`):
// the player, equalizer and playlist windows as one unit.
//
// SE-4 owns two halves:
//   * Part A — the *assembly*: the app instantiates all three windows and the
//     toggle/close seams are routed here, so the EQ window is live in the
//     binary rather than dead code.
//   * Part B — the snap/dock/z-order manager and per-skin layout persistence.
//
// Snapping: on drag end (`SkinnedWindow::dragFinished`) the moved window is
// pulled to a sibling's edge/corner within `kSnapTolerancePx` (Winamp's 10 px).
// Windows that end up flush-adjacent are *docked*.
//
// Docking: the player window is the dock anchor. Dragging the anchor carries
// every window docked to it (transitively) — the group moves as one, no
// flicker. Dragging an attached EQ/playlist window detaches it from the group
// (the rest stay put); releasing it flush to a sibling re-docks it. This is the
// "un-drag on detach" gesture.
//
// Layout: positions, windowshade states and visibility round-trip through
// `skins::SkinDb`'s per-skin `SkinLayoutState` (exact restore on launch).
#pragma once

#include <array>
#include <filesystem>
#include <vector>

#include <QEvent>
#include <QObject>
#include <QPoint>
#include <QRect>

#include "skins/SkinDb.hpp"
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
    static constexpr int kWindowCount = 3;

    /// Winamp's snap tolerance: pull a dragged window to a sibling edge/corner.
    static constexpr int kSnapTolerancePx = 10;
    /// Flush-adjacency tolerance for dock detection (device pixels).
    static constexpr int kDockTolerancePx = 1;

    WindowSet(MainWindow& main, EqWindow& equalizer, PlaylistWindow& playlist,
              QObject* parent = nullptr);
    ~WindowSet() override;

    /// Wires the seams the set owns: the main window's EQ/PL toggles show/hide
    /// their windows, each secondary close button hides it, and every window's
    /// `dragFinished` feeds the snap manager. Idempotent.
    void attach();

    /// Shows the whole set (all three windows). Winamp opens them all.
    void showAll();

    /// Places the set in the classic arrangement — the EQ flush against the
    /// player's right edge, the playlist flush beneath it — centred on the
    /// primary screen's available area and kept on it. Used when a skin has no
    /// saved layout (a first run), so the app opens in the shape users expect
    /// and the set is docked from the start (the first drag of the player then
    /// carries the group). Geometry only: visibility stays with `showAll()` and
    /// `restoreLayout()`. Returns the arrangement's bounding rect.
    QRect applyDefaultArrangement();

    SkinnedWindow* window(WindowId id) const;
    MainWindow& mainWindow() { return m_main; }
    EqWindow& eqWindow() { return m_eq; }
    PlaylistWindow& playlistWindow() { return m_playlist; }

    // --- view ---------------------------------------------------------------
    /// Applies an integer scale (1..4) to every window in the set, the visualizer
    /// included: the classic windows clamp it themselves.
    void setScale(int scale);

    // --- the visualizer (VZ-1) ----------------------------------------------
    /// Registers the visualizer window. It is deliberately *not* one of the
    /// classic three: it does not dock, `showAll()` does not open it, and the
    /// group drag ignores it (`indexOf()` reports -1, so the event filter and the
    /// dock groups never see it). What the set does own is snapping it to its
    /// siblings and remembering where — and whether — it was open.
    ///
    /// Type-erased to `SkinnedWindow` so this header never needs the canvas types
    /// and the GL-free build stays valid.
    void setVisualizerWindow(SkinnedWindow* window);
    SkinnedWindow* visualizerWindow() const { return m_visualizer; }
    /// Shows/hides the visualizer and records the intent for the layout (the
    /// window itself is usually hidden at quit, so `isVisible()` is not usable as
    /// the record — the same reason the classic three keep intent flags).
    void setVisualizerVisible(bool visible);
    bool isVisualizerVisible() const { return m_visualizerVisible; }
    /// Snaps the visualizer to the visible classic windows (its drag end).
    void snapVisualizer();

    // --- snapping / docking -------------------------------------------------
    void setSnapTolerance(int px);
    int snapTolerance() const { return m_snapTolerance; }

    /// The dock anchor whose drag carries the group (default Main).
    void setDockAnchor(WindowId id) { m_anchor = id; }
    WindowId dockAnchor() const { return m_anchor; }

    /// Snap `moved` to its siblings and rebuild the dock groups. Runs
    /// automatically on drag end; public so scripted tests can drive it.
    void snapAfterDrag(WindowId moved);
    /// Rebuilds the dock groups from current visible geometry.
    void recomputeDockGroups();

    /// The windows docked to `id` (including `id`), sorted by id.
    std::vector<WindowId> dockGroup(WindowId id) const;
    /// Every dock group with more than one member.
    std::vector<std::vector<WindowId>> dockGroups() const;
    bool docked(WindowId a, WindowId b) const;

    // --- z-order ------------------------------------------------------------
    /// Keep-together always-on-top for the whole set (`config_aot`).
    void setKeepOnTop(bool on);
    bool keepOnTop() const { return m_keepOnTop; }

    // --- layout persistence -------------------------------------------------
    /// Snapshot of the current positions, shades, playlist size and visibility.
    skins::SkinLayoutState layout() const;
    /// Applies a stored layout (shades, visibility, positions, playlist size).
    void applyLayout(const skins::SkinLayoutState& state);
    /// Restores the layout recorded for `skin`, if any. Returns whether one was.
    bool restoreLayout(const skins::SkinDb& db, const std::filesystem::path& skin);
    /// Records the current layout for `skin` (creates the record on first use).
    void persistLayout(skins::SkinDb& db, const std::filesystem::path& skin) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Index (0..2) of a window, or -1 when it is not part of the set.
    int indexOf(const SkinnedWindow* window) const;
    static WindowId idAt(int index);
    SkinnedWindow* widget(WindowId id) const;
    bool isShown(WindowId id) const;
    /// Shows/hides a window and records the set's visibility intent (Qt hides
    /// toplevels as the event loop unwinds, so layout() must not read
    /// `isVisible()` at quit time).
    void setWindowVisible(WindowId id, bool visible);
    static bool adjacent(const QRect& a, const QRect& b);
    /// Per-axis pull of `rect` toward the nearest visible sibling edges.
    QPoint snapAdjustment(WindowId moved, const QRect& rect) const;
    /// The same search against explicit sibling rects, for a window outside the
    /// classic three (the visualizer: every visible classic window is a sibling).
    QPoint snapAdjustmentFor(const QRect& rect, const std::vector<QRect>& siblings) const;
    /// Rects of the visible classic windows.
    std::vector<QRect> visibleClassicRects() const;
    QRect rectOf(WindowId id) const;

    MainWindow& m_main;
    EqWindow& m_eq;
    PlaylistWindow& m_playlist;
    bool m_attached = false;

    SkinnedWindow* m_visualizer = nullptr;
    bool m_visualizerVisible = false;
    bool m_visualizerAttached = false;

    int m_snapTolerance = kSnapTolerancePx;
    WindowId m_anchor = WindowId::Main;
    bool m_keepOnTop = false;
    std::vector<std::vector<WindowId>> m_groups;
    /// Visibility intent (the set controls show/hide); the source of truth for
    /// layout persistence and dock participation.
    std::array<bool, kWindowCount> m_visible{true, true, true};

    // Live group drag (all on the GUI thread).
    bool m_dragActive = false;
    WindowId m_dragLeader = WindowId::Main;
    std::vector<WindowId> m_followers;
    bool m_applying = false;
    std::array<QPoint, kWindowCount> m_lastPos{};
};

}  // namespace koalaamp::ui
