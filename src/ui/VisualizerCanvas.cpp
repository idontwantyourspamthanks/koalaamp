// SPDX-License-Identifier: MIT
#include "ui/VisualizerCanvas.hpp"

#include <QOpenGLContext>

#include <algorithm>
#include <utility>

namespace koalaamp::ui {
namespace {

/// Drain the ring in bounded chunks: projectM accepts at most
/// projectm_pcm_get_max_samples() per call, and this must not allocate per frame.
constexpr std::size_t kDrainFrames = 4096;

}  // namespace

VisualizerCanvas::VisualizerCanvas(QWidget* parent)
    : QOpenGLWidget(parent),
      m_engine(interop::visualizer::createEngine()),
      m_scratch(kDrainFrames * 2, 0.0F) {}

VisualizerCanvas::~VisualizerCanvas() = default;

void VisualizerCanvas::initializeGL() {
    // The engine's objects belong to this context, and the teardown has to run while
    // the context is still current — aboutToBeDestroyed is the last moment that is
    // true, and it arrives before the widget's own destructor.
    if (QOpenGLContext* glContext = context()) {
        connect(glContext, &QOpenGLContext::aboutToBeDestroyed, this, [this] {
            makeCurrent();
            m_engine->destroy();
            m_created = false;
            doneCurrent();
        });
    }
    const qreal ratio = devicePixelRatio();
    m_created = m_engine->create(std::max(1, static_cast<int>(width() * ratio)),
                                 std::max(1, static_cast<int>(height() * ratio)));
}

void VisualizerCanvas::resizeGL(int width, int height) {
    Q_UNUSED(width);
    Q_UNUSED(height);
    pushSizeToEngine();
}

void VisualizerCanvas::pushSizeToEngine() {
    if (!m_created) {
        return;
    }
    const qreal ratio = devicePixelRatio();
    m_engine->resize(std::max(1, static_cast<int>(width() * ratio)),
                     std::max(1, static_cast<int>(height() * ratio)));
}

void VisualizerCanvas::requestPreset(const std::string& path) {
    m_pendingPreset = path;
    update();  // paintGL applies it, with the context current
}

void VisualizerCanvas::paintGL() {
    if (!m_pendingPreset.empty()) {
        const std::string path = std::move(m_pendingPreset);
        m_pendingPreset.clear();
        m_engine->loadPresetFile(path, /*smoothTransition=*/true);
    }
    if (!m_created) {
        return;
    }
    // Everything that arrived since the previous frame, oldest first. readNew()
    // consumes, so projectM is never handed the same audio twice.
    std::size_t frames = 0;
    while ((frames = m_ring.readNew(m_scratch.data(), kDrainFrames)) > 0) {
        m_engine->pushPcm(m_scratch.data(), frames);
    }
    m_engine->render(static_cast<std::uint32_t>(defaultFramebufferObject()));
}

}  // namespace koalaamp::ui
