// SPDX-License-Identifier: MIT
#include "ui/VisualizerCanvas.hpp"

#include <QOpenGLContext>
#include <QShowEvent>

#include <algorithm>
#include <utility>

#include "interop/projectm/VisualizerEngine.hpp"

namespace koalaamp::ui {
namespace {

/// Drain the ring in bounded chunks: projectM accepts at most
/// projectm_pcm_get_max_samples() per call, and this must not allocate per frame.
constexpr std::size_t kDrainFrames = 4096;

/// The animation clock. A QOpenGLWidget repaints on expose/resize only, so an
/// untimed preset renders once and then sits frozen; ~60 fps matches projectM's
/// configured rate.
constexpr int kFrameIntervalMs = 16;

}  // namespace

VisualizerCanvas::VisualizerCanvas(interop::visualizer::IVisualizerEngine& engine,
                                   QWidget* parent)
    : QOpenGLWidget(parent), m_engine(engine), m_scratch(kDrainFrames * 2, 0.0F) {
    m_frameClock.setInterval(kFrameIntervalMs);
    connect(&m_frameClock, &QTimer::timeout, this, qOverload<>(&QWidget::update));
}

VisualizerCanvas::~VisualizerCanvas() = default;

void VisualizerCanvas::initializeGL() {
    // The engine's objects belong to this context, and the teardown has to run while
    // the context is still current — aboutToBeDestroyed is the last moment that is
    // true, and it arrives before the widget's own destructor.
    if (QOpenGLContext* glContext = context()) {
        connect(glContext, &QOpenGLContext::aboutToBeDestroyed, this, [this] {
            makeCurrent();
            m_engine.destroy();
            m_created = false;
            doneCurrent();
        });
    }
    const qreal ratio = devicePixelRatio();
    m_created = m_engine.create(std::max(1, static_cast<int>(width() * ratio)),
                                std::max(1, static_cast<int>(height() * ratio)));
}

void VisualizerCanvas::resizeGL(int width, int height) {
    Q_UNUSED(width);
    Q_UNUSED(height);
    pushSizeToEngine();
}

void VisualizerCanvas::showEvent(QShowEvent* event) {
    QOpenGLWidget::showEvent(event);
    m_frameClock.start();
}

void VisualizerCanvas::hideEvent(QHideEvent* event) {
    m_frameClock.stop();
    QOpenGLWidget::hideEvent(event);
}

void VisualizerCanvas::pushSizeToEngine() {
    if (!m_created) {
        return;
    }
    const qreal ratio = devicePixelRatio();
    m_engine.resize(std::max(1, static_cast<int>(width() * ratio)),
                    std::max(1, static_cast<int>(height() * ratio)));
}

playback::IAudioEngine::PcmCb VisualizerCanvas::pcmCallback() {
    return [this](const float* interleaved, std::size_t frames,
                  const playback::PcmFormat& format) {
        // Audio thread: no allocation, no lock, no GUI.
        m_ring.push(interleaved, frames, format.channels);
    };
}

void VisualizerCanvas::requestPreset(const std::string& path) {
    m_pendingPreset = path;
    update();  // paintGL applies it, with the context current
}

void VisualizerCanvas::paintGL() {
    if (!m_pendingPreset.empty()) {
        const std::string path = std::move(m_pendingPreset);
        m_pendingPreset.clear();
        m_engine.loadPresetFile(path, /*smoothTransition=*/true);
    }
    if (!m_created) {
        return;
    }
    // Everything that arrived since the previous frame, oldest first — drained
    // whether or not a preset is loaded, so the ring's cursor never lags behind the
    // audio thread. readNew() consumes, so projectM is never handed audio twice.
    std::size_t frames = 0;
    while ((frames = m_ring.readNew(m_scratch.data(), kDrainFrames)) > 0) {
        m_engine.pushPcm(m_scratch.data(), frames);
    }
    m_engine.render(static_cast<std::uint32_t>(defaultFramebufferObject()));
    ++m_frames;
}

}  // namespace koalaamp::ui
