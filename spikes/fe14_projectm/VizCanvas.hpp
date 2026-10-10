// SPDX-License-Identifier: MIT
// FE-14 spike — hosting libprojectM inside a QOpenGLWidget (the supported Qt
// integration pattern). Throwaway spike code; the shipping visualiser is VZ-1.
//
// projectM is created in initializeGL() (GL context current), fed synthetic PCM
// on a timer, and rendered in paintGL() into the widget's own framebuffer via
// projectm_opengl_render_frame_fbo() — the entry point that takes Qt's
// defaultFramebufferObject(). (4.1.x's projectm_opengl_render_frame() hardcodes
// FBO 0 and produces an empty widget; see docs/spikes/projectm-qt.md.)
#pragma once

#include "SyntheticPcm.hpp"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <projectM-4/logging.h>
#include <projectM-4/projectM.h>
#include <vector>

namespace fe14 {

class VizCanvas : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit VizCanvas(QWidget* parent = nullptr);
    ~VizCanvas() override;

    void setPresetFiles(const QStringList& files);
    void setCycleIntervalMs(int ms);
    void setSmoothTransitions(bool smooth) { smoothTransitions_ = smooth; }

    bool projectmReady() const { return pm_ != nullptr; }
    QString glInfo() const { return glInfo_; }
    QJsonObject metrics() const;

    // Stop the timers and destroy the projectM instance with the GL context
    // current. Idempotent; call before tearing down the application.
    void stop();

    // Called (via an extern "C" trampoline) when projectM rejects a preset.
    void notePresetFailure(const QString& file, const QString& message);

    // Called (via an extern "C" trampoline) for projectM log messages.
    void noteLogMessage(int level, const QString& message);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    struct PresetStats {
        QString name;
        double loadMs = 0.0;
        qint64 frames = 0;
        qint64 blackFrames = 0;
        double activeSeconds = 0.0;
        double sumLuma = 0.0;
    };

    void pushAudio();
    void loadPreset(int index, bool smooth);
    void advancePreset();
    void applyPendingPreset();
    double sampleLuma();
    void closeBlackRun(qint64 nowNs);

    projectm_handle pm_ = nullptr;
    QString glInfo_;
    QStringList presetFiles_;
    int current_ = -1;
    int cycleMs_ = 5000;
    bool smoothTransitions_ = true;
    int pendingPreset_ = -1; // set by the cycle timer, executed in paintGL
    QStringList switchFailures_;
    QStringList logMessages_;

    QTimer audioTimer_;
    QTimer renderTimer_;
    QTimer cycleTimer_;

    SyntheticPcm pcm_;
    std::vector<float> audioBuf_;
    QElapsedTimer clock_;
    qint64 lastAudioNs_ = 0;

    QElapsedTimer frameClock_;
    qint64 lastFrameNs_ = -1;
    qint64 totalFrames_ = 0;
    GLuint lastFbo_ = 0;
    int glErrors_ = 0;

    QVector<PresetStats> stats_;

    // Black-frame instrumentation: a black "run" longer than 100 ms across a
    // preset switch is the failure mode the acceptance criteria name.
    qint64 blackRunStartNs_ = -1;
    double maxBlackRunMs_ = 0.0;
    qint64 switchStartNs_ = 0;
    double maxSwitchBlackMs_ = 0.0;
    double switchFirstVisibleMs_ = -1.0;
    double maxSwitchFirstVisibleMs_ = 0.0;

    double lastLoadMs_ = 0.0;
    double sumLoadMs_ = 0.0;
    qint64 loadCount_ = 0;
    qint64 presetStartNs_ = 0;

    static constexpr double kBlackLuma = 4.0;            // mean 0..255
    static constexpr qint64 kSwitchWindowNs = 750000000; // 750 ms after a load
};

} // namespace fe14
