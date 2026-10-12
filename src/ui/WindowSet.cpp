// SPDX-License-Identifier: MIT
#include "ui/WindowSet.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "ui/PleditState.hpp"

namespace koalaamp::ui {

namespace {

/// Inclusive rect edges for adjacency/snap maths.
int right(const QRect& r) { return r.x() + r.width(); }
int bottom(const QRect& r) { return r.y() + r.height(); }

}  // namespace

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
                     [this](bool on) { setWindowVisible(WindowId::Eq, on); });
    QObject::connect(&m_main, &MainWindow::playlistToggled, &m_playlist,
                     [this](bool on) { setWindowVisible(WindowId::Playlist, on); });

    // Each secondary window's own close button hides it (Winamp behaviour).
    QObject::connect(&m_eq, &EqWindow::closeRequested, &m_eq,
                     [this] { setWindowVisible(WindowId::Eq, false); });
    QObject::connect(&m_playlist, &PlaylistWindow::closeRequested, &m_playlist,
                     [this] { setWindowVisible(WindowId::Playlist, false); });

    // Snap manager: every window's drag end runs the snap pass, and live moves
    // drive the group drag.
    QObject::connect(&m_main, &SkinnedWindow::dragFinished, this,
                     [this] { snapAfterDrag(WindowId::Main); });
    QObject::connect(&m_eq, &SkinnedWindow::dragFinished, this,
                     [this] { snapAfterDrag(WindowId::Eq); });
    QObject::connect(&m_playlist, &SkinnedWindow::dragFinished, this,
                     [this] { snapAfterDrag(WindowId::Playlist); });

    for (int i = 0; i < kWindowCount; ++i) {
        SkinnedWindow* w = widget(idAt(i));
        w->installEventFilter(this);
        m_lastPos[static_cast<std::size_t>(i)] = w->pos();
    }
    recomputeDockGroups();
}

void WindowSet::showAll() {
    for (int i = 0; i < kWindowCount; ++i) {
        setWindowVisible(idAt(i), true);
    }
    for (int i = 0; i < kWindowCount; ++i) {
        m_lastPos[static_cast<std::size_t>(i)] = widget(idAt(i))->pos();
    }
    recomputeDockGroups();
}

SkinnedWindow* WindowSet::window(WindowId id) const { return widget(id); }

// ---------------------------------------------------------------------------
// Dock groups / snapping
// ---------------------------------------------------------------------------

int WindowSet::indexOf(const SkinnedWindow* window) const {
    for (int i = 0; i < kWindowCount; ++i) {
        if (widget(idAt(i)) == window) {
            return i;
        }
    }
    return -1;
}

WindowSet::WindowId WindowSet::idAt(int index) {
    switch (index) {
        case 0:
            return WindowId::Main;
        case 1:
            return WindowId::Eq;
        default:
            return WindowId::Playlist;
    }
}

SkinnedWindow* WindowSet::widget(WindowId id) const {
    switch (id) {
        case WindowId::Main:
            return const_cast<MainWindow*>(&m_main);
        case WindowId::Eq:
            return const_cast<EqWindow*>(&m_eq);
        case WindowId::Playlist:
            return const_cast<PlaylistWindow*>(&m_playlist);
    }
    return nullptr;
}

void WindowSet::setWindowVisible(WindowId id, bool visible) {
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kWindowCount) {
        return;
    }
    m_visible[static_cast<std::size_t>(index)] = visible;
    SkinnedWindow* w = widget(id);
    if (visible) {
        w->show();
    } else {
        w->hide();
    }
    recomputeDockGroups();
}

bool WindowSet::isShown(WindowId id) const {
    const int index = static_cast<int>(id);
    return index >= 0 && index < kWindowCount && m_visible[static_cast<std::size_t>(index)];
}

QRect WindowSet::rectOf(WindowId id) const {
    const SkinnedWindow* w = widget(id);
    return QRect(w->pos(), w->size());
}

bool WindowSet::adjacent(const QRect& a, const QRect& b) {
    const int t = kDockTolerancePx;
    const bool vNear = a.y() <= bottom(b) + t && b.y() <= bottom(a) + t;
    const bool hTouch = std::abs(a.x() - right(b)) <= t || std::abs(b.x() - right(a)) <= t;
    const bool hNear = a.x() <= right(b) + t && b.x() <= right(a) + t;
    const bool vTouch = std::abs(a.y() - bottom(b)) <= t || std::abs(b.y() - bottom(a)) <= t;
    return (hTouch && vNear) || (vTouch && hNear);
}

void WindowSet::recomputeDockGroups() {
    // Union-find over the visible windows.
    int parent[kWindowCount] = {0, 1, 2};
    const auto findRoot = [&parent](int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    for (int i = 0; i < kWindowCount; ++i) {
        for (int j = i + 1; j < kWindowCount; ++j) {
            if (!isShown(idAt(i)) || !isShown(idAt(j))) {
                continue;
            }
            if (adjacent(rectOf(idAt(i)), rectOf(idAt(j)))) {
                const int ri = findRoot(i);
                const int rj = findRoot(j);
                if (ri != rj) {
                    parent[ri] = rj;
                }
            }
        }
    }
    std::vector<std::vector<WindowId>> buckets(kWindowCount);
    for (int i = 0; i < kWindowCount; ++i) {
        buckets[static_cast<std::size_t>(findRoot(i))].push_back(idAt(i));
    }
    m_groups.clear();
    for (auto& bucket : buckets) {
        if (bucket.size() > 1) {
            m_groups.push_back(std::move(bucket));
        }
    }
}

std::vector<WindowSet::WindowId> WindowSet::dockGroup(WindowId id) const {
    for (const auto& group : m_groups) {
        if (std::find(group.begin(), group.end(), id) != group.end()) {
            return group;
        }
    }
    return {id};
}

std::vector<std::vector<WindowSet::WindowId>> WindowSet::dockGroups() const { return m_groups; }

bool WindowSet::docked(WindowId a, WindowId b) const {
    if (a == b) {
        return false;
    }
    const auto group = dockGroup(a);
    return std::find(group.begin(), group.end(), b) != group.end();
}

void WindowSet::setSnapTolerance(int px) { m_snapTolerance = std::max(0, px); }

QPoint WindowSet::snapAdjustment(WindowId moved, const QRect& r) const {
    int bestDx = 0;
    int bestDy = 0;
    int bestAbsX = m_snapTolerance + 1;
    int bestAbsY = m_snapTolerance + 1;
    for (int i = 0; i < kWindowCount; ++i) {
        const WindowId other = idAt(i);
        if (other == moved || !isShown(other)) {
            continue;
        }
        const QRect s = rectOf(other);
        const int xTargets[4] = {s.x(), s.x() + s.width() - r.width(), s.x() - r.width(),
                                 right(s)};
        for (const int target : xTargets) {
            const int d = target - r.x();
            const int ad = std::abs(d);
            if (ad <= m_snapTolerance && ad < bestAbsX) {
                bestAbsX = ad;
                bestDx = d;
            }
        }
        const int yTargets[4] = {s.y(), s.y() + s.height() - r.height(), s.y() - r.height(),
                                 bottom(s)};
        for (const int target : yTargets) {
            const int d = target - r.y();
            const int ad = std::abs(d);
            if (ad <= m_snapTolerance && ad < bestAbsY) {
                bestAbsY = ad;
                bestDy = d;
            }
        }
    }
    return {bestDx, bestDy};
}

void WindowSet::snapAfterDrag(WindowId moved) {
    m_dragActive = false;
    m_followers.clear();

    const QPoint adjust = snapAdjustment(moved, rectOf(moved));
    if (!adjust.isNull()) {
        // Shift the whole current group so its relative offsets stay exact.
        m_applying = true;
        for (const WindowId member : dockGroup(moved)) {
            SkinnedWindow* w = widget(member);
            w->move(w->pos() + adjust);
        }
        m_applying = false;
    }
    for (int i = 0; i < kWindowCount; ++i) {
        m_lastPos[static_cast<std::size_t>(i)] = widget(idAt(i))->pos();
    }
    recomputeDockGroups();
}

// ---------------------------------------------------------------------------
// Z-order
// ---------------------------------------------------------------------------

void WindowSet::setKeepOnTop(bool on) {
    m_keepOnTop = on;
    m_main.setAlwaysOnTop(on);
    m_eq.setAlwaysOnTop(on);
    m_playlist.setAlwaysOnTop(on);
}

// ---------------------------------------------------------------------------
// Layout persistence
// ---------------------------------------------------------------------------

skins::SkinLayoutState WindowSet::layout() const {
    skins::SkinLayoutState state;
    state.main = {m_main.x(), m_main.y()};
    state.equalizer = {m_eq.x(), m_eq.y()};
    state.playlist = {m_playlist.x(), m_playlist.y()};
    // Store the *unshaded* content size so the shade toggle round-trips.
    state.playlistWidth = pleditWidth(m_playlist.state());
    state.playlistHeight = pleditHeight(m_playlist.state());
    state.mainShade = m_main.isShaded();
    state.eqShade = m_eq.isShaded();
    state.playlistShade = m_playlist.isShaded();
    state.mainVisible = isShown(WindowId::Main);
    state.eqVisible = isShown(WindowId::Eq);
    state.playlistVisible = isShown(WindowId::Playlist);
    return state;
}

void WindowSet::applyLayout(const skins::SkinLayoutState& state) {
    m_applying = true;

    m_main.setShade(state.mainShade);
    m_eq.setShade(state.eqShade);
    m_playlist.applyGeometry(
        PleditGeometry{state.playlistWidth, state.playlistHeight, state.playlistShade});

    setWindowVisible(WindowId::Main, state.mainVisible);
    setWindowVisible(WindowId::Eq, state.eqVisible);
    setWindowVisible(WindowId::Playlist, state.playlistVisible);

    m_main.move(state.main.x, state.main.y);
    m_eq.move(state.equalizer.x, state.equalizer.y);
    m_playlist.move(state.playlist.x, state.playlist.y);

    m_applying = false;
    for (int i = 0; i < kWindowCount; ++i) {
        m_lastPos[static_cast<std::size_t>(i)] = widget(idAt(i))->pos();
    }
    recomputeDockGroups();
}

bool WindowSet::restoreLayout(const skins::SkinDb& db, const std::filesystem::path& skin) {
    const skins::SkinLayoutState* record = db.layout(skin);
    if (record == nullptr) {
        return false;
    }
    applyLayout(*record);
    return true;
}

void WindowSet::persistLayout(skins::SkinDb& db, const std::filesystem::path& skin) const {
    db.layoutFor(skin) = layout();
}

// ---------------------------------------------------------------------------
// Event filter: live group drag
// ---------------------------------------------------------------------------

bool WindowSet::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() != QEvent::Move) {
        return QObject::eventFilter(watched, event);
    }
    auto* w = qobject_cast<SkinnedWindow*>(watched);
    if (w == nullptr) {
        return false;
    }
    const int index = indexOf(w);
    if (index < 0) {
        return false;
    }
    const QPoint now = w->pos();
    const QPoint delta = now - m_lastPos[static_cast<std::size_t>(index)];
    m_lastPos[static_cast<std::size_t>(index)] = now;
    if (m_applying) {
        return false;
    }

    if (!w->isDragging()) {
        m_dragActive = false;
        return false;
    }

    const WindowId id = idAt(index);
    if (!w->isDragging()) {
        // A follower moved by the group drag is not itself dragging; only the
        // leader ending its drag clears the session.
        if (m_dragActive && id == m_dragLeader) {
            m_dragActive = false;
        }
        return false;
    }

    if (!m_dragActive || m_dragLeader != id) {
        m_dragActive = true;
        m_dragLeader = id;
        m_followers.clear();
        // The anchor carries its docked group; any other window detaches (moves
        // alone) — the un-drag-on-detach gesture.
        if (id == m_anchor) {
            for (const WindowId member : dockGroup(id)) {
                if (member != id) {
                    m_followers.push_back(member);
                }
            }
        }
    }
    if (!delta.isNull() && !m_followers.empty()) {
        m_applying = true;
        for (const WindowId member : m_followers) {
            SkinnedWindow* f = widget(member);
            f->move(f->pos() + delta);
        }
        m_applying = false;
    }
    return false;
}

}  // namespace koalaamp::ui
