// SPDX-License-Identifier: MIT
//
// PresetCycler — see PresetCycler.hpp for the two-choice policy and the ticket.
#include "ui/PresetCycler.hpp"

#include <algorithm>

namespace koalaamp::ui {

PresetCycler::PresetCycler(PresetStore& store, QObject* parent)
    : QObject(parent), m_store(store) {
    m_timer.setInterval(m_intervalMs);
    QObject::connect(&m_timer, &QTimer::timeout, this, [this] {
        if (!m_locked && !m_store.empty()) {
            m_store.next();
        }
    });
    // A folder that appears later — or one that has just become empty — changes
    // whether there is anything to cycle through at all.
    QObject::connect(&m_store, &PresetStore::listChanged, this, [this] { restartTimer(); });
    m_store.setOrder(m_order);
    restartTimer();
}

void PresetCycler::setOrder(PresetOrder order) {
    if (order == m_order) {
        return;
    }
    m_order = order;
    m_store.setOrder(order);
}

void PresetCycler::toggleOrder() {
    setOrder(m_order == PresetOrder::Ordered ? PresetOrder::Random : PresetOrder::Ordered);
}

void PresetCycler::setLocked(bool locked) {
    if (locked == m_locked) {
        return;
    }
    m_locked = locked;
    restartTimer();
}

void PresetCycler::toggleLocked() {
    setLocked(!m_locked);
}

void PresetCycler::setIntervalMs(int ms) {
    if (ms == m_intervalMs) {
        return;
    }
    m_intervalMs = ms;
    m_timer.setInterval(std::max(1, ms));
    restartTimer();
}

bool PresetCycler::next() {
    return m_store.next();
}

bool PresetCycler::previous() {
    return m_store.previous();
}

void PresetCycler::restartTimer() {
    m_timer.stop();
    if (!m_locked && m_intervalMs > 0 && !m_store.empty()) {
        m_timer.start();
    }
}

}  // namespace koalaamp::ui
