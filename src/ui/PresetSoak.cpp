// SPDX-License-Identifier: MIT
//
// PresetSoak — see PresetSoak.hpp for the contract and the ticket reference.
#include "ui/PresetSoak.hpp"

#include <algorithm>
#include <cstdio>

namespace koalaamp::ui {

PresetSoak::PresetSoak(PresetStore& store, PresetCycler& cycler, FrameCountFn frames,
                       QObject* parent)
    : QObject(parent), m_store(store), m_cycler(cycler), m_frames(std::move(frames)) {
    QObject::connect(&m_heartbeat, &QTimer::timeout, this, [this] { tick(); });
}

void PresetSoak::start(int holdMs) {
    if (m_running) {
        return;
    }
    m_holdMs = std::max(1, holdMs);
    m_remaining = m_store.size();
    if (m_store.empty()) {
        log(QStringLiteral("soak: no presets — nothing to do"));
        emit finished(0, QStringLiteral("empty store"));
        return;
    }

    m_running = true;
    m_stalled = false;
    // The soak drives the cycler itself (one clock), so the cycler must not run
    // its own timer; ordered, so every preset is seen exactly once.
    m_cycler.setOrder(PresetOrder::Ordered);
    m_cycler.setLocked(true);

    m_framesAtPresetStart = m_frames();
    m_framesAtLastTick = m_framesAtPresetStart;
    m_since.start();
    m_sinceLastFrame.start();
    m_heartbeat.setInterval(std::max(1, m_holdMs / 4));
    m_heartbeat.start();

    log(QStringLiteral("soak: %1 preset(s), %2 ms each, first is \"%3\"")
            .arg(m_remaining)
            .arg(m_holdMs)
            .arg(QString::fromStdString(PresetStore::presetDisplayName(m_store.current()))));
}

void PresetSoak::setStallMs(int ms) {
    m_stallMs = std::max(1, ms);
}

void PresetSoak::tick() {
    if (!m_running) {
        return;
    }

    const std::uint64_t frames = m_frames();
    if (frames != m_framesAtLastTick) {
        m_framesAtLastTick = frames;
        m_sinceLastFrame.restart();
    }
    if (m_sinceLastFrame.elapsed() >= m_stallMs) {
        m_stalled = true;
        log(QStringLiteral("STALL: \"%1\" rendered no new frame for %2 ms")
                .arg(QString::fromStdString(PresetStore::presetDisplayName(m_store.current())))
                .arg(m_sinceLastFrame.elapsed()));
        const QString reason = QStringLiteral("stalled on %1")
                                   .arg(QString::fromStdString(m_store.current()));
        stop();
        emit finished(2, reason);
        return;
    }

    if (m_since.elapsed() < m_holdMs) {
        return;
    }

    const auto drawn = frames - m_framesAtPresetStart;
    const double seconds = static_cast<double>(m_since.elapsed()) / 1000.0;
    log(QStringLiteral("%1/%2  \"%3\"  %4 frames  %5 fps")
            .arg(m_store.size() - m_remaining + 1)
            .arg(m_store.size())
            .arg(QString::fromStdString(PresetStore::presetDisplayName(m_store.current())))
            .arg(drawn)
            .arg(seconds > 0.0 ? static_cast<double>(drawn) / seconds : 0.0, 0, 'f', 1));

    m_framesAtPresetStart = frames;
    m_since.restart();
    if (--m_remaining == 0) {
        log(QStringLiteral("soak: complete, %1 preset(s) held").arg(m_store.size()));
        stop();
        emit finished(0, QStringLiteral("complete"));
        return;
    }
    m_cycler.next();
}

void PresetSoak::log(const QString& line) {
    const std::string text = line.toStdString();
    m_lines.push_back(text);
    // Straight to stdout and flushed: a crash later must not lose the log of what
    // ran before it.
    std::fprintf(stdout, "%s\n", text.c_str());
    std::fflush(stdout);
}

void PresetSoak::stop() {
    m_running = false;
    m_heartbeat.stop();
}

}  // namespace koalaamp::ui
