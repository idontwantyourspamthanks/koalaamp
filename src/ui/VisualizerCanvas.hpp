// SPDX-License-Identifier: MIT
//
// VisualizerCanvas — the GL surface the visualizer window shows. It owns the
// visualizer engine, drains the visualizer's PCM ring into it, and applies queued
// preset loads with the context current.
//
// FE-14's two hard-won rules are encoded here (docs/spikes/projectm-qt.md):
//   * the engine is created in initializeGL and torn down from the context's
//     aboutToBeDestroyed — never from a destructor, which can run long after the
//     context has gone (and offscreen tests would never notice);
//   * preset loads are *queued* and applied inside paintGL, because loading from a
//     timer callback has no current context and ends in permanent black output.
//
// Playback is never disturbed: the ring is written by the audio thread and drops its
// oldest frames if this GL thread falls behind, so nothing here ever waits on audio.
#pragma once

#include <QOpenGLWidget>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "interop/projectm/VisualizerEngine.hpp"
#include "ui/StereoPcmRing.hpp"

namespace koalaamp::ui {

class VisualizerCanvas : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit VisualizerCanvas(QWidget* parent = nullptr);
    ~VisualizerCanvas() override;

    /// The ring the app's PCM fan-out writes into — the producer end is the audio
    /// thread, the consumer is paintGL.
    StereoPcmRing& pcmRing() { return m_ring; }

    /// Ask for a preset. Applied on the next paintGL, with the GL context current.
    void requestPreset(const std::string& path);

    /// The engine this build has: available() is false when the façade was compiled
    /// without libprojectM, and lastError() says so — which is what the window shows.
    interop::visualizer::IVisualizerEngine& engine() { return *m_engine; }
    const interop::visualizer::IVisualizerEngine& engine() const { return *m_engine; }

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;

private:
    /// projectM renders in device pixels, so the logical widget size is not what it
    /// wants; this pushes the scaled size.
    void pushSizeToEngine();

    std::unique_ptr<interop::visualizer::IVisualizerEngine> m_engine;
    StereoPcmRing m_ring;
    std::vector<float> m_scratch;  ///< stereo frames, sized once: no per-frame alloc
    std::string m_pendingPreset;   ///< applied in paintGL
    bool m_created = false;
};

}  // namespace koalaamp::ui
