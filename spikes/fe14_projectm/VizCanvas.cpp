// SPDX-License-Identifier: MIT
// FE-14 spike — libprojectM inside QOpenGLWidget. See VizCanvas.hpp.
#include "VizCanvas.hpp"

#include <QFileInfo>
#include <QJsonArray>
#include <QOpenGLContext>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fe14 {

namespace {
// projectM's preset-switch-failed callback has C linkage; trampoline back into
// the instance carried in user_data.
extern "C" void fe14PresetSwitchFailed(const char* filename, const char* message, void* userData) {
    auto* canvas = static_cast<VizCanvas*>(userData);
    if (canvas != nullptr) {
        canvas->notePresetFailure(QString::fromUtf8(filename != nullptr ? filename : ""),
                                  QString::fromUtf8(message != nullptr ? message : ""));
    }
}

extern "C" void fe14LogCallback(const char* message, projectm_log_level level, void* userData) {
    auto* canvas = static_cast<VizCanvas*>(userData);
    if (canvas != nullptr) {
        canvas->noteLogMessage(static_cast<int>(level),
                               QString::fromUtf8(message != nullptr ? message : ""));
    }
}
} // namespace

VizCanvas::VizCanvas(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
}

VizCanvas::~VizCanvas() {
    stop();
}

void VizCanvas::setPresetFiles(const QStringList& files) {
    presetFiles_ = files;
}

void VizCanvas::setCycleIntervalMs(int ms) {
    cycleMs_ = ms;
}

void VizCanvas::notePresetFailure(const QString& file, const QString& message) {
    switchFailures_.append(QFileInfo(file).fileName() + QStringLiteral(": ") + message);
}

void VizCanvas::noteLogMessage(int level, const QString& message) {
    if (logMessages_.size() >= 500) {
        return;
    }
    static const char* const kNames[] = {"NOTSET", "TRACE", "DEBUG", "INFO",
                                         "WARN",   "ERROR", "FATAL"};
    const char* name = (level >= 0 && level <= 6) ? kNames[level] : "?";
    logMessages_.append(QString::fromLatin1(name) + QStringLiteral(": ") + message.trimmed());
}

void VizCanvas::initializeGL() {
    initializeOpenGLFunctions();

    glInfo_ = QStringLiteral("renderer=%1; version=%2")
                  .arg(QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_RENDERER))),
                       QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_VERSION))));

    pm_ = projectm_create();
    if (pm_ == nullptr) {
        qWarning("FE-14 spike: projectm_create() returned null (no current GL context?)");
        return;
    }

    // Capture projectM warnings/errors (unsupported HLSL features, shader
    // compile failures, ...) — the evidence for "misbehaving" presets. Level
    // is overridable via FE14_PROJECTM_LOG=info|debug|trace for diagnostics.
    projectm_set_log_callback(&fe14LogCallback, false, this);
    int logLevel = PROJECTM_LOG_LEVEL_WARN;
    const QByteArray wantLevel = qgetenv("FE14_PROJECTM_LOG").toLower();
    if (wantLevel == "info") {
        logLevel = PROJECTM_LOG_LEVEL_INFO;
    } else if (wantLevel == "debug") {
        logLevel = PROJECTM_LOG_LEVEL_DEBUG;
    } else if (wantLevel == "trace") {
        logLevel = PROJECTM_LOG_LEVEL_TRACE;
    }
    projectm_set_log_level(static_cast<projectm_log_level>(logLevel), false);

    const auto pw =
        static_cast<size_t>(std::max(1, static_cast<int>(width() * devicePixelRatioF())));
    const auto ph =
        static_cast<size_t>(std::max(1, static_cast<int>(height() * devicePixelRatioF())));
    projectm_set_window_size(pm_, pw, ph);
    projectm_set_mesh_size(pm_, 48, 32);
    projectm_set_fps(pm_, 60);
    projectm_set_aspect_correction(pm_, true);
    projectm_set_beat_sensitivity(pm_, 1.0f);
    projectm_set_preset_duration(pm_, 3600.0); // the spike drives cycling itself
    projectm_set_preset_switch_failed_event_callback(pm_, &fe14PresetSwitchFailed, this);

    audioBuf_.resize(static_cast<size_t>(projectm_pcm_get_max_samples()) * 2);

    clock_.start();
    frameClock_.start();
    lastAudioNs_ = clock_.nsecsElapsed();
    lastFrameNs_ = -1;

    if (!presetFiles_.isEmpty()) {
        stats_.resize(presetFiles_.size());
        for (int i = 0; i < presetFiles_.size(); ++i) {
            stats_[i].name = QFileInfo(presetFiles_.at(i)).fileName();
        }
        loadPreset(0, false);
    }

    audioTimer_.setTimerType(Qt::PreciseTimer);
    audioTimer_.setInterval(20);
    connect(&audioTimer_, &QTimer::timeout, this, [this] { pushAudio(); });
    audioTimer_.start();

    // Repaint as fast as the loop allows; the timer interval of 0 keeps the GL
    // thread busy (software GL under tests, vsync on hardware).
    renderTimer_.setInterval(0);
    connect(&renderTimer_, &QTimer::timeout, this, [this] { update(); });
    renderTimer_.start();

    if (presetFiles_.size() > 1) {
        cycleTimer_.setInterval(cycleMs_);
        connect(&cycleTimer_, &QTimer::timeout, this, [this] { advancePreset(); });
        cycleTimer_.start();
    }
}

void VizCanvas::resizeGL(int w, int h) {
    if (pm_ == nullptr) {
        return;
    }
    const auto pw = static_cast<size_t>(std::max(1, static_cast<int>(w * devicePixelRatioF())));
    const auto ph = static_cast<size_t>(std::max(1, static_cast<int>(h * devicePixelRatioF())));
    projectm_set_window_size(pm_, pw, ph);
}

void VizCanvas::pushAudio() {
    if (pm_ == nullptr) {
        return;
    }
    const qint64 now = clock_.nsecsElapsed();
    double dt = static_cast<double>(now - lastAudioNs_) / 1e9;
    lastAudioNs_ = now;
    if (dt <= 0.0) {
        return;
    }
    dt = std::min(dt, 0.2); // never push more than 200 ms at once

    const size_t maxFrames = audioBuf_.size() / 2;
    size_t frames = static_cast<size_t>(dt * pcm_.sampleRate());
    while (frames > 0) {
        const size_t chunk = std::min(frames, maxFrames);
        pcm_.fill(audioBuf_.data(), chunk);
        projectm_pcm_add_float(pm_, audioBuf_.data(), static_cast<unsigned>(chunk),
                               PROJECTM_STEREO);
        frames -= chunk;
    }
}

void VizCanvas::loadPreset(int index, bool smooth) {
    if (pm_ == nullptr || index < 0 || index >= presetFiles_.size()) {
        return;
    }
    const qint64 now = clock_.nsecsElapsed();
    if (current_ >= 0) {
        stats_[current_].activeSeconds += static_cast<double>(now - presetStartNs_) / 1e9;
    }

    current_ = index;
    presetStartNs_ = now;

    QElapsedTimer timer;
    timer.start();
    projectm_load_preset_file(pm_, presetFiles_.at(index).toUtf8().constData(), smooth);
    lastLoadMs_ = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    sumLoadMs_ += lastLoadMs_;
    ++loadCount_;
    stats_[index].loadMs = lastLoadMs_;

    // Start the black-frame watchdog window for this switch.
    switchStartNs_ = now;
    switchFirstVisibleMs_ = -1.0;
}

void VizCanvas::advancePreset() {
    if (presetFiles_.isEmpty()) {
        return;
    }
    // projectM compiles shaders / creates GL textures on preset load, so the
    // load MUST run with the GL context current — queue it for paintGL rather
    // than calling into projectM from a timer callback (no current context).
    pendingPreset_ = (current_ + 1) % presetFiles_.size();
    update();
}

void VizCanvas::applyPendingPreset() {
    if (pendingPreset_ < 0) {
        return;
    }
    const int index = pendingPreset_;
    pendingPreset_ = -1;
    loadPreset(index, smoothTransitions_);
}

double VizCanvas::sampleLuma() {
    const auto w = std::max(1, static_cast<int>(width() * devicePixelRatioF()));
    const auto h = std::max(1, static_cast<int>(height() * devicePixelRatioF()));
    constexpr int kSize = 32;
    const int x = std::max(0, w / 2 - kSize / 2);
    const int y = std::max(0, h / 2 - kSize / 2);

    unsigned char pixels[kSize * kSize * 4];
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(defaultFramebufferObject()));
    glReadPixels(x, y, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    double sum = 0.0;
    for (int i = 0; i < kSize * kSize; ++i) {
        sum += (pixels[4 * i] + pixels[4 * i + 1] + pixels[4 * i + 2]) / 3.0;
    }
    return sum / (kSize * kSize);
}

void VizCanvas::closeBlackRun(qint64 nowNs) {
    if (blackRunStartNs_ < 0) {
        return;
    }
    const double ms = static_cast<double>(nowNs - blackRunStartNs_) / 1e6;
    maxBlackRunMs_ = std::max(maxBlackRunMs_, ms);
    // Attribute to the switch only if this black run *started* inside the
    // watchdog window that opens when a preset is loaded.
    if (switchStartNs_ > 0 && blackRunStartNs_ >= switchStartNs_ &&
        blackRunStartNs_ - switchStartNs_ <= kSwitchWindowNs) {
        maxSwitchBlackMs_ = std::max(maxSwitchBlackMs_, ms);
    }
    blackRunStartNs_ = -1;
}

void VizCanvas::paintGL() {
    if (pm_ == nullptr) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    // Preset switches are executed here, where Qt guarantees the GL context is
    // current (loading a preset compiles shaders and creates GL textures).
    applyPendingPreset();

    if (current_ < 0) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    projectm_opengl_render_frame_fbo(pm_, static_cast<uint32_t>(defaultFramebufferObject()));

    const qint64 now = frameClock_.nsecsElapsed();
    lastFrameNs_ = now;
    ++totalFrames_;

    const GLuint fbo = static_cast<GLuint>(defaultFramebufferObject());
    if (fbo != lastFbo_) {
        std::fprintf(stderr, "[fe14] widget FBO changed %u -> %u at frame %lld\n", lastFbo_, fbo,
                     static_cast<long long>(totalFrames_));
        lastFbo_ = fbo;
    }
    const GLenum glError = glGetError();
    if (glError != GL_NO_ERROR && glErrors_ < 20) {
        std::fprintf(stderr, "[fe14] GL error 0x%x at frame %lld\n", glError,
                     static_cast<long long>(totalFrames_));
        ++glErrors_;
    }

    const double luma = sampleLuma();
    if (current_ < stats_.size()) {
        PresetStats& s = stats_[current_];
        ++s.frames;
        s.sumLuma += luma;
        if (luma < kBlackLuma) {
            ++s.blackFrames;
        }
    }
    if (luma < kBlackLuma) {
        if (blackRunStartNs_ < 0) {
            blackRunStartNs_ = now;
            std::fprintf(stderr, "[fe14] black run starts at frame %lld (preset %s)\n",
                         static_cast<long long>(totalFrames_),
                         current_ >= 0 ? qPrintable(presetFiles_.at(current_)) : "?");
        }
    } else {
        closeBlackRun(now);
        if (switchFirstVisibleMs_ < 0.0 && switchStartNs_ > 0) {
            switchFirstVisibleMs_ = static_cast<double>(now - switchStartNs_) / 1e6;
            maxSwitchFirstVisibleMs_ = std::max(maxSwitchFirstVisibleMs_, switchFirstVisibleMs_);
        }
    }
}

void VizCanvas::stop() {
    audioTimer_.stop();
    renderTimer_.stop();
    cycleTimer_.stop();

    if (current_ >= 0) {
        const qint64 now = clock_.nsecsElapsed();
        stats_[current_].activeSeconds += static_cast<double>(now - presetStartNs_) / 1e9;
        closeBlackRun(now);
        current_ = -1;
    }

    if (pm_ != nullptr) {
        if (isValid() && context() != nullptr) {
            makeCurrent();
        }
        projectm_destroy(pm_);
        pm_ = nullptr;
        if (context() != nullptr && context()->isValid()) {
            doneCurrent();
        }
    }
}

QJsonObject VizCanvas::metrics() const {
    QJsonObject out;
    out["gl"] = glInfo_;
    out["preset_count"] = presetFiles_.size();
    out["cycle_ms"] = cycleMs_;
    out["frames"] = static_cast<double>(totalFrames_);
    if (lastFrameNs_ > 0) {
        out["avg_fps"] =
            static_cast<double>(totalFrames_) * 1e9 / static_cast<double>(lastFrameNs_);
    }
    out["load_count"] = static_cast<double>(loadCount_);
    out["last_load_ms"] = lastLoadMs_;
    out["avg_load_ms"] = loadCount_ > 0 ? sumLoadMs_ / static_cast<double>(loadCount_) : 0.0;
    out["max_black_run_ms"] = maxBlackRunMs_;
    out["max_switch_black_ms"] = maxSwitchBlackMs_;
    out["max_switch_to_visible_ms"] = maxSwitchFirstVisibleMs_;

    QJsonArray failures;
    for (const QString& f : switchFailures_) {
        failures.append(f);
    }
    out["failed_presets"] = failures;

    QJsonArray logs;
    for (const QString& l : logMessages_) {
        logs.append(l);
    }
    out["log_messages"] = logs;

    QJsonArray presets;
    for (const PresetStats& s : stats_) {
        QJsonObject p;
        p["name"] = s.name;
        p["load_ms"] = s.loadMs;
        p["frames"] = static_cast<double>(s.frames);
        p["fps"] = s.activeSeconds > 0.0 ? static_cast<double>(s.frames) / s.activeSeconds : 0.0;
        p["black_frames"] = static_cast<double>(s.blackFrames);
        p["mean_luma"] = s.frames > 0 ? s.sumLuma / static_cast<double>(s.frames) : 0.0;
        presets.append(p);
    }
    out["presets"] = presets;
    return out;
}

} // namespace fe14
