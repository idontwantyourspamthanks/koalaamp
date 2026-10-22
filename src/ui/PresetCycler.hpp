// SPDX-License-Identifier: MIT
//
// PresetCycler — the visualizer's cycling policy (VZ-1 task 4): which preset comes
// next, and when the window advances on its own.
//
// Two independent choices, the way Winamp and MilkDrop treat them:
//   * the order — Ordered walks the store's sorted list, Random jumps around (the
//     store owns both, and never repeats the current preset in Random);
//   * locked — the auto-advance timer stops, so the current preset stays until the
//     user moves it. Manual next/previous work in every mode.
//
// The selection itself lives in PresetStore, so nothing here duplicates the list:
// the cycler only decides when to call `next()`.
//
// docs/spec.md §6.1; docs/tickets/VZ-1-visualizer.md task 4.
#pragma once

#include "ui/PresetStore.hpp"

#include <QObject>
#include <QTimer>

namespace koalaamp::ui {

class PresetCycler : public QObject {
    Q_OBJECT

public:
    /// A new preset every 30 s — long enough to watch one, short enough to notice.
    static constexpr int kDefaultIntervalMs = 30000;

    explicit PresetCycler(PresetStore& store, QObject* parent = nullptr);

    // --- order ---------------------------------------------------------------
    PresetOrder order() const { return m_order; }
    void setOrder(PresetOrder order);
    /// The R hotkey: Ordered <-> Random.
    void toggleOrder();

    // --- auto-advance --------------------------------------------------------
    bool isLocked() const { return m_locked; }
    /// Locked stops the timer; manual advances still work.
    void setLocked(bool locked);
    /// The L hotkey: locked <-> unlocked.
    void toggleLocked();
    /// True while the auto-advance timer is running.
    bool isCycling() const { return m_timer.isActive(); }

    int intervalMs() const { return m_intervalMs; }
    /// A non-positive interval disables auto-advance (like being locked).
    void setIntervalMs(int ms);

    // --- advancing -----------------------------------------------------------
    /// Manual advance; false when the store has nothing to move to.
    bool next();
    bool previous();

signals:
    /// The policy changed (including through `toggleOrder`/`toggleLocked`):
    /// the on-screen toggles mirror these, not their own click state.
    void orderChanged(PresetOrder order);
    void lockedChanged(bool locked);

private:
    void restartTimer();

    PresetStore& m_store;
    PresetOrder m_order = PresetOrder::Ordered;
    bool m_locked = false;
    int m_intervalMs = kDefaultIntervalMs;
    QTimer m_timer;
};

} // namespace koalaamp::ui
