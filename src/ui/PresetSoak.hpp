// SPDX-License-Identifier: MIT
//
// PresetSoak — the nightly preset soak (VZ-1 task 8): hold each preset for a
// while, log what it rendered, and say so when one stops rendering.
//
// The ticket wants a crash/hang/FPS log over a large corpus. Crash is the
// process dying (the nightly job's log is the evidence), hang is a preset that
// stops producing frames while the clock keeps running, and FPS is frames per
// hold — so this class only needs the store, the cycler and a frame counter.
//
// That counter is a std::function rather than a window pointer on purpose: the
// visualizer window only exists in a build with Qt OpenGLWidgets, and this
// driver is plain Qt, so it stays testable without a GL context.
//
// docs/tickets/VZ-1-visualizer.md task 8.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include "ui/PresetCycler.hpp"
#include "ui/PresetStore.hpp"

namespace koalaamp::ui {

class PresetSoak : public QObject {
    Q_OBJECT

public:
    /// How many frames the renderer has drawn so far.
    using FrameCountFn = std::function<std::uint64_t()>;

    /// No new frame for this long means the preset has hung. It is a fixed window,
    /// not a fraction of the hold: a slow first frame (llvmpipe warming up) must
    /// not read as a stall, and the ticket's holds are ten seconds.
    static constexpr int kStallMs = 5000;
    /// Shorter for tests, which must not idle for five seconds to see a stall.
    void setStallMs(int ms);
    int stallMs() const { return m_stallMs; }

    /// `frames` must outlive the soak. `store` and `cycler` are driven by it: the
    /// cycler ends up ordered and locked, because the soak is the single clock.
    PresetSoak(PresetStore& store, PresetCycler& cycler, FrameCountFn frames,
               QObject* parent = nullptr);

    /// Each line as it is written (also emitted to stdout immediately, so a crash
    /// still leaves the log of what ran before it).
    const std::vector<std::string>& lines() const { return m_lines; }

    /// True once a preset has stopped rendering.
    bool stalled() const { return m_stalled; }

    /// Walks every preset once, holding each for `holdMs`. Does nothing (and
    /// reports success) when the store is empty.
    void start(int holdMs);

signals:
    /// Emitted once: 0 when every preset was held, 2 when one stalled.
    void finished(int exitCode, const QString& reason);

private:
    void tick();
    void log(const QString& line);
    void stop();

    PresetStore& m_store;
    PresetCycler& m_cycler;
    FrameCountFn m_frames;
    std::vector<std::string> m_lines;

    QElapsedTimer m_since;
    QElapsedTimer m_sinceLastFrame;
    QTimer m_heartbeat;
    std::uint64_t m_framesAtPresetStart = 0;
    std::uint64_t m_framesAtLastTick = 0;
    std::size_t m_remaining = 0;
    std::uint64_t m_framesAtLastTickBase = 0;
    int m_holdMs = 0;
    int m_stallMs = kStallMs;
    bool m_running = false;
    bool m_stalled = false;
};

}  // namespace koalaamp::ui
