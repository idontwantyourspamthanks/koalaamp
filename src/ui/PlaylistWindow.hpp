// SPDX-License-Identifier: MIT
//
// The classic Winamp playlist editor: a `pledit.bmp`-skinned, segment-tiled,
// resizable window with drag-reorder rows, classic multi-select, the J
// "jump to file" dialog with the real queue, right-click Winamp verbs and the
// windowshade bar — the second skinned window, built on SE-2's patterns
// (docs/tickets/SE-3-playlist-window.md, docs/spec.md §5.4).
//
// Layout: `PlaylistWindow` (frameless toplevel) -> `PleditCanvas` child,
// transparent to mouse events so the window owns all input. It drives
// `PlaybackCore` through the `ITransport` seam; it never touches
// src/playback.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <QPoint>
#include <QTimer>

#include "skins/SkinSet.hpp"
#include "ui/PleditCanvas.hpp"
#include "ui/PleditHitRegion.hpp"
#include "ui/PleditState.hpp"
#include "ui/SkinnedWindow.hpp"
#include "ui/Transport.hpp"

class QMenu;

namespace koalaamp::ui {

class JumpToFileDialog;

/// Playlist `Font=` fallback: rows use the system family only when it is
/// actually installed; otherwise the 5x6 `text.bmp` font (classic behaviour,
/// and what keeps the committed goldens platform-stable).
class PlaylistWindow : public SkinnedWindow {
    Q_OBJECT

public:
    explicit PlaylistWindow(QWidget* parent = nullptr);
    ~PlaylistWindow() override;

    // --- skin ---------------------------------------------------------------
    void setSkin(const skins::SkinSet* skin);
    const skins::SkinSet* skin() const { return m_skin; }

    // --- playback -----------------------------------------------------------
    void setTransport(ITransport* transport);
    ITransport* transport() const { return m_transport; }
    /// Reconciles the playing row and the queue view with the transport.
    void syncFromTransport();

    // --- model --------------------------------------------------------------
    const PleditState& state() const { return m_state; }
    void setState(const PleditState& state);
    void setEntries(std::vector<PleditEntry> entries);
    void addEntries(std::vector<PleditEntry> entries);
    /// Adds media by path/URL, deriving the display title through the ATF seam.
    void addMediaFiles(const std::vector<std::string>& paths);
    std::size_t entryCount() const { return m_state.entries.size(); }
    /// Re-formats every entry's title with `format` (the `titlefmt` verb).
    void setTitleFormat(std::string format);
    const std::string& titleFormat() const { return m_titleFormat; }

    // --- view ---------------------------------------------------------------
    int scale() const { return m_scale; }
    void setScale(int scale);
    void setShade(bool shaded);
    bool isShaded() const { return m_state.shade; }
    /// Keep-on-top for the window set (`config_aot`, SE-4); a flag change
    /// recreates the native window, so the position is restored.
    void setAlwaysOnTop(bool on);
    bool isAlwaysOnTop() const;
    void setSegments(int widthSegments, int heightSegments);
    int widthSegments() const { return m_state.widthSegments; }
    int heightSegments() const { return m_state.heightSegments; }
    /// Stored window geometry (SkinLayoutState round-trip).
    PleditGeometry geometry() const { return pleditGeometry(m_state); }
    void applyGeometry(const PleditGeometry& geometry);

    void setJumpDialogEnabled(bool on) { m_jumpEnabled = on; }
    bool jumpDialogEnabled() const { return m_jumpEnabled; }
    bool jumpDialogVisible() const;
    /// Opens (or focuses) the J dialog. Exposed for tests.
    void openJumpDialog();
    JumpToFileDialog* jumpDialog() const { return m_jumpDialog.get(); }

    /// Right-click verb menu. Exposed so the parity suite can assert its verbs.
    QMenu* activeMenu() const { return m_menu; }
    void openContextMenu(const QPoint& localPoint, int row);
    void closeMenu();

    // --- verbs (also reachable from the menus) ------------------------------
    void playRow(std::size_t index);
    void enqueueRow(std::size_t index);
    void enqueueSelected();
    /// JTFE "load playlist into queue": clears then enqueues every entry.
    void loadIntoQueue();
    void clearQueue();
    void removeSelected();
    void removeRow(std::size_t index);
    void selectAllRows();
    /// Scrolls the list by `rows` (negative scrolls up).
    void scrollBy(int rows);

    PleditCanvas& canvas() { return *m_canvas; }
    const PleditCanvas& canvas() const { return *m_canvas; }

    /// Animation/idle tick: reconciles queue + current row. Exposed for tests.
    void tick();

    /// The interaction-parity checklist this window implements; each id has a
    /// scripted-input test in tests/se3_interaction_test.cpp.
    static const std::vector<std::string>& interactionChecklist();

signals:
    void closeRequested();
    void shadeChanged(bool shaded);
    void alwaysOnTopChanged(bool on);
    void mediaOpenRequested();  ///< eject: add files (the file dialog host)
    void trackActivated(int index);
    void entriesAdded(int count);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool isTitleBarAt(const QPoint& point) const override;

private:
    enum class Drag { None, Resize, ScrollHandle, Rows };

    void buildUi();
    void applySkinToCanvas();
    void updateShape();
    void requestRepaint();
    void retitleEntries();

    PleditHitId controlAt(const QPoint& point) const;
    void pressControl(PleditHitId id, const QPoint& point);
    void dragControl(const QPoint& point);
    void releaseControl(PleditHitId id, const QPoint& point);
    void activate(PleditHitId id);

    void pressRow(int row, Qt::KeyboardModifiers modifiers);
    void beginRowDrag(int row);
    void finishRowDrag();
    void updateHover(const QPoint& point);

    void openMenuFor(PleditHitId id, const QPoint& localPoint);

    void scrollFromPoint(int localY);
    void playSelectedOrFirst();

    const skins::SkinSet* m_skin = nullptr;
    ITransport* m_transport = nullptr;
    PleditCanvas* m_canvas = nullptr;
    PleditState m_state;
    int m_scale = 1;
    std::string m_titleFormat;

    // --- input state ---
    Drag m_drag = Drag::None;
    PleditHitId m_pressed = PleditHitId::None;
    int m_anchor = 0;         ///< shift-range anchor row
    int m_dragRow = -1;       ///< row the drag started on
    int m_dropRow = -1;       ///< current drop target
    bool m_rowDragActive = false;
    QPoint m_pressGlobal;
    int m_resizeStartW = 0;
    int m_resizeStartH = 0;
    int m_scrollStartOffset = 0;
    QPoint m_scrollStartGlobal;

    QMenu* m_menu = nullptr;
    std::unique_ptr<JumpToFileDialog> m_jumpDialog;
    bool m_jumpEnabled = true;

    QTimer m_timer;
};

/// The complete interaction-parity list (see interactionChecklist()).
const std::vector<std::string>& playlistInteractionChecklist();

}  // namespace koalaamp::ui
