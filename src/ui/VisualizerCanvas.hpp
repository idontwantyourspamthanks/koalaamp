// SPDX-License-Identifier: MIT
//
// VisualizerCanvas — the GL surface the visualizer window shows. It drives the
// caller's engine: it drains the visualizer's PCM ring into it and applies queued
// preset loads with the context current.
//
// FE-14's rules are encoded here (docs/spikes/projectm-qt.md):
//   * the engine is created in initializeGL and torn down from the context's
//     aboutToBeDestroyed — never from a destructor, which can run long after the
//     context has gone (and which no offscreen test would ever notice);
//   * preset loads are *queued* and applied inside paintGL, because loading from a
//     timer callback has no current context and ends in permanent black output.
//
// It also owns the frame clock the widget would otherwise lack: a QOpenGLWidget
// repaints only on expose/resize, so without a timer the preset would render one
// frame and sit frozen — a symptom that looks exactly like a dead audio feed.
// The timer runs only while the widget is shown.
//
// The engine is *not* created here: this module has no dependency on the rendering
// back end beyond the seam's interface, so the app (which links the faade and its
// RPATH) decides what renders. That keeps third-party headers and libraries out of
// every UI consumer.
#pragma once

#include <QOpenGLWidget>
#include <QTimer>

#include <cstddef>
#include <string>
#include <vector>

#include "playback/IAudioEngine.hpp"
#include "ui/StereoPcmRing.hpp"

namespace koalaamp::interop::visualizer {
class IVisualizerEngine;
}  // namespace koalaamp::interop::visualizer

namespace koalaamp::ui {

class VisualizerCanvas : public QOpenGLWidget {
    Q_OBJECT

public:
    /// `engine` must outlive the canvas (the app owns it; the canvas brings it up in
    /// initializeGL and tears it down with the context).
    explicit VisualizerCanvas(interop::visualizer::IVisualizerEngine& engine,
                              QWidget* parent = nullptr);
    ~VisualizerCanvas() override;

    /// The ring the app's PCM fan-out writes into — producer: the audio thread,
    /// consumer: paintGL.
    StereoPcmRing& pcmRing() { return m_ring; }

    /// The callback to hand to `PcmFanout`: it pushes the same post-DSP frames the
    /// spectrum sees into `pcmRing()`. Audio-thread rules apply (no allocation, no
    /// lock, no GUI) — see StereoPcmRing.
    playback::IAudioEngine::PcmCb pcmCallback();

    /// Ask for a preset. Applied on the next paintGL, with the GL context current.
    void requestPreset(const std::string& path);

    /// Frames rendered since construction — the soak's liveness signal (a preset
    /// that stops producing frames is what "hang" means here).
    std::uint64_t framesRendered() const { return m_frames; }

    /// The engine being driven. available() is false in a build without libprojectM,
    /// and lastError() says so — which is what the window shows.
    interop::visualizer::IVisualizerEngine& engine() { return m_engine; }
    const interop::visualizer::IVisualizerEngine& engine() const { return m_engine; }

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    /// projectM renders in device pixels, so the logical widget size is not what it
    /// wants; this pushes the scaled size.
    void pushSizeToEngine();

    interop::visualizer::IVisualizerEngine& m_engine;
    StereoPcmRing m_ring;
    std::vector<float> m_scratch;  ///< stereo frames, sized once: no per-frame alloc
    QTimer m_frameClock;           ///< ~60 fps while shown
    std::string m_pendingPreset;   ///< applied in paintGL
    bool m_created = false;
    std::uint64_t m_frames = 0;
};

}  // namespace koalaamp::ui
