// SPDX-License-Identifier: MIT
#include "ui/MainWindow.hpp"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include "ui/MediaName.hpp"
#include "ui/PlayerRenderer.hpp"
#include "ui/TitleText.hpp"

namespace koalaamp::ui {

namespace {

using L = MainLayout;
using S = ShadeLayout;

/// Winamp's startup user message shown in the marquee before anything is
/// loaded (Webamp uses its own version string the same way).
constexpr std::string_view kStartupMessage = "KoalaAmp";

/// Audio extensions the eject dialog offers.
const char* kAudioFilter =
    "Audio (*.mp3 *.mp2 *.mp1 *.ogg *.oga *.flac *.wav *.m4a *.aac *.wma *.opus *.aiff "
    "*.ape *.wv *.mpc *.mid *.mod *.xm *.s3m *.it);;All files (*)";

int valueFromX(int localX, int trackLeft, int trackWidth, int thumbWidth, int minValue,
               int maxValue) {
    const int span = std::max(1, trackWidth - thumbWidth);
    const double fraction =
        std::clamp(static_cast<double>(localX - trackLeft) / static_cast<double>(span), 0.0, 1.0);
    return minValue + static_cast<int>(std::lround(fraction * (maxValue - minValue)));
}

const std::vector<std::string>& checklistStorage() {
    static const std::vector<std::string> items = {
        // Transport
        "play-click-starts-or-restarts",
        "pause-click-toggles-pause",
        "stop-click-stops-and-rewinds",
        "next-click-advances",
        "previous-click-calls-transport-previous",
        "eject-click-requests-media",
        "transport-press-shows-active-sprite",
        // Toggles
        "shuffle-click-toggles-and-renders-selected",
        "shuffle-press-shows-depressed-sprite",
        "repeat-click-cycles-all-then-off",
        "repeat-press-shows-depressed-sprite",
        "eq-toggle-renders-four-states",
        "playlist-toggle-renders-four-states",
        "eq-click-emits-toggled",
        "playlist-click-emits-toggled",
        // Seek bar
        "seekbar-click-seeks",
        "seekbar-drag-scrubs",
        "seekbar-drag-while-paused-stays-paused",
        "seekbar-thumb-hidden-when-stopped",
        "seekbar-thumb-sprite-selected-while-dragging",
        // Volume / balance
        "volume-drag-sets-percent",
        "volume-wheel-steps-by-two",
        "volume-thumb-selected-while-dragging",
        "volume-frame-follows-percent",
        "balance-drag-sets-pan",
        "balance-wheel-steps-by-two",
        "balance-centre-snaps-to-zero",
        "balance-frame-off-by-nine-groove",
        // Indicating fields
        "time-click-toggles-elapsed-remaining",
        "time-hidden-while-stopped",
        "time-countdown-minus-sign",
        "monostereo-stereo-from-2-channel-pcm",
        "monostereo-mono-from-1-channel-pcm",
        "khz-from-pcm-sample-rate",
        "kbps-placeholder-when-unknown",
        // Visualiser
        "vis-click-cycles-spectrum-osc-off",
        "vis-hidden-while-stopped",
        "vis-bars-follow-pcm",
        // Title bar / clutter bar
        "titlebar-drag-moves-window",
        "titlebar-double-click-toggles-shade",
        "shade-button-toggles-shade",
        "shade-uses-275x14-geometry",
        "shade-mini-time-click-toggles-time-mode",
        "shade-position-drag-seeks",
        "minimize-click-minimises",
        "close-click-requests-close",
        "menu-button-requests-menu",
        "clutter-a-toggles-always-on-top",
        "clutter-d-toggles-double-size",
        "clutter-i-requests-file-info",
        "clutter-o-requests-menu",
        "clutter-v-toggles-visualiser",
        // Title marquee
        "marquee-scrolls-long-titles",
        "marquee-static-for-short-titles",
        "marquee-falls-back-to-space-for-unknown-glyphs",
        // Keyboard
        "key-x-plays-key-c-pauses",
        "key-v-stops-key-z-and-b-step",
        "key-arrows-seek-and-change-volume",
        "key-s-and-r-toggle-shuffle-repeat",
        "key-l-opens-a-file",
        // Skin
        "region-mask-applied-in-both-states",
        "double-size-is-integer-nearest-neighbour",
    };
    return items;
}

}  // namespace

const std::vector<std::string>& interactionParityChecklist() { return checklistStorage(); }

const std::vector<std::string>& MainWindow::interactionChecklist() { return checklistStorage(); }

MainWindow::MainWindow(QWidget* parent) : SkinnedWindow(parent) {
    buildUi();
    setWindowTitle(QStringLiteral("KoalaAmp"));
    m_state.title = std::string(kStartupMessage);
    m_state.marqueeOffsetPx = marqueeScrollPx(m_state.title, m_marqueeStep);
    updateShape();
    applySkinToCanvas();
    syncFromTransport();

    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    connect(this, &SkinnedWindow::shadeToggleRequested, this,
            [this] { setShade(!m_state.shade); });
    updateTickTimer();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    m_canvas = new SkinCanvas(this);
    // The window owns every pointer event; the canvas is a pure paint surface.
    m_canvas->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_canvas->move(0, 0);
}

void MainWindow::setSkin(const skins::SkinSet* skin) {
    m_skin = skin;
    applySkinToCanvas();
    updateShape();
    requestRepaint();
}

void MainWindow::applySkinToCanvas() {
    static const skins::VisColor fallback = skins::defaultVisColor();
    if (m_skin != nullptr) {
        m_canvas->setSkin(&m_skin->sheets, m_skin->visColor);
    } else {
        m_canvas->setSkin(nullptr, fallback);
    }
}

void MainWindow::setTransport(ITransport* transport) {
    m_transport = transport;
    m_syntheticVisual = transport == nullptr;
    m_visFeed.reset();
    syncFromTransport();
    updateTickTimer();
}

void MainWindow::setSyntheticVisual(bool on) {
    m_syntheticVisual = on;
    if (on) {
        m_visFeed.reset();
    }
}

void MainWindow::syncFromTransport() {
    if (m_transport != nullptr) {
        const playback::PlaybackState status = m_transport->state();
        m_state.status = status == playback::PlaybackState::Playing
                             ? PlayerStatus::Playing
                             : status == playback::PlaybackState::Paused ? PlayerStatus::Paused
                                                                         : PlayerStatus::Stopped;
        const bool hasMedia = m_transport->hasMedia();
        const std::int64_t position = m_transport->positionMs();
        const std::int64_t length = m_transport->lengthMs();
        const std::string title = m_transport->title();
        if (hasMedia && !title.empty()) {
            m_mediaTitle = title;
            m_state.title = m_mediaTitle;
        } else if (m_state.title.empty()) {
            m_state.title = std::string(kStartupMessage);
        }
        m_state.timeVisible = m_state.status != PlayerStatus::Stopped;
        const std::int64_t shown = m_state.countdown && length > 0 ? length - position : position;
        m_state.timeSeconds = static_cast<int>(std::max<std::int64_t>(0, shown) / 1000);
        m_state.position =
            length > 0 ? std::clamp(static_cast<int>(std::lround(
                                        static_cast<double>(position) * 100.0 /
                                        static_cast<double>(length))),
                                    0, 100)
                       : 0;
        if (m_dragging != HitId::Volume && m_dragging != HitId::Balance) {
            m_state.volume = m_transport->volumePercent();
        }
        m_state.shuffle = m_transport->shuffle();
        m_state.repeat = m_transport->repeatMode() != playback::RepeatMode::Off;
    }
    // Stream format is observed from the PCM tap, which is also what the
    // monoster indicator and the khz field report.
    const int channels = m_visFeed.channels();
    const int sampleRate = m_visFeed.sampleRate();
    m_state.channels = channels;
    m_state.khzKnown = sampleRate > 0;
    m_state.khz = sampleRate / 1000;
    const int bitrate = m_transport != nullptr ? m_transport->bitrateKbps() : 0;
    m_state.kbpsKnown = bitrate > 0;
    m_state.kbps = std::max(0, bitrate);
    m_state.marqueeOffsetPx = marqueeScrollPx(m_state.title, m_marqueeStep);
    // A transport event can flip the status under us (the engine starts
    // asynchronously), so re-evaluate the poll timer before painting — the
    // timer is what advances the position, and it only runs while the status
    // says something is happening. The header's contract for this method is
    // "pulls the transport's state into the render state and repaints".
    updateTickTimer();
    requestRepaint();
}

void MainWindow::setState(const PlayerState& state) {
    setRenderState(state);
}

void MainWindow::setRenderState(const PlayerState& state) {
    const bool shadeMoved = state.shade != m_state.shade;
    m_state = state;
    m_state.marqueeOffsetPx = marqueeScrollPx(m_state.title, m_marqueeStep);
    if (shadeMoved) {
        updateShape();
    }
    requestRepaint();
    updateTickTimer();
}

void MainWindow::requestRepaint() {
    m_canvas->setState(m_state);
    m_canvas->setFixedSize(m_canvas->contentSize());
    update();
}

void MainWindow::setScale(int scale) {
    const int clamped = std::clamp(scale, 1, 4);
    if (clamped == m_scale) {
        return;
    }
    m_scale = clamped;
    m_canvas->setScale(clamped);
    updateShape();
    requestRepaint();
}

void MainWindow::setDoubleSize(bool on) {
    m_state.clutter.d = on;
    setScale(on ? 2 : 1);
    emit doubleSizeChanged(on);
}

void MainWindow::setShade(bool shaded) {
    if (shaded == m_state.shade) {
        return;
    }
    m_state.shade = shaded;
    updateShape();
    requestRepaint();
    updateTickTimer();
    emit shadeChanged(shaded);
}

void MainWindow::setAlwaysOnTop(bool on) {
    m_state.clutter.a = on;
    const QPoint position = pos();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    // Qt recreates the native window when a flag changes; restore the place.
    show();
    move(position);
    requestRepaint();
    emit alwaysOnTopChanged(on);
}

bool MainWindow::isAlwaysOnTop() const {
    return (windowFlags() & Qt::WindowStaysOnTopHint) != 0;
}

void MainWindow::updateShape() {
    const QSize content(kMainWidth * m_scale,
                        (m_state.shade ? kShadeHeight : kMainHeight) * m_scale);
    static const skins::Region none;
    const skins::Region& region = m_skin != nullptr ? m_skin->region : none;
    setWindowShape(region, m_state.shade, content, m_scale);
    m_canvas->setFixedSize(content);
    m_canvas->setScale(m_scale);
}

void MainWindow::updateTickTimer() {
    const bool wantAnimation = m_state.status != PlayerStatus::Stopped ||
                               marqueeScrolls(m_state.title) ||
                               (m_state.status == PlayerStatus::Paused);
    if (wantAnimation && !m_timer.isActive()) {
        m_timer.start();
    } else if (!wantAnimation && m_timer.isActive()) {
        m_timer.stop();
    }
}

void MainWindow::tick() {
    syncFromTransport();
    m_animationMs += kTickMs;
    if (m_state.status == PlayerStatus::Paused) {
        m_pauseMs += kTickMs;
    } else {
        m_pauseMs = 0;
    }

    // Marquee: one cell per 220 ms, matching Webamp's STEP_MARQUEE.
    if (marqueeScrolls(m_state.title)) {
        const qint64 steps = m_animationMs / kMarqueeStepMs;
        if (steps != m_marqueeStep) {
            m_marqueeStep = static_cast<int>(steps);
        }
    }

    // Pause blink: 2 s cycle, `step-start`, 1 s delay → visible for the first
    // half of every cycle (Webamp's `blink` keyframes).
    m_state.timeBlinkOn = m_state.status != PlayerStatus::Paused || (m_pauseMs % 2000) < 1000;

    // Visualiser samples the PCM tap (or a synthetic feed offscreen) once per
    // tick while a track is playing.
    if (m_state.status == PlayerStatus::Playing && m_state.vis != VisMode::Off) {
        if (m_syntheticVisual) {
            m_state.visFrame = syntheticVisFrame(m_visStep);
        } else {
            m_visFeed.sample(m_state.visFrame);
        }
        ++m_visStep;
    } else if (m_state.status != PlayerStatus::Playing) {
        m_state.visFrame.clear();
    }

    requestRepaint();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

HitId MainWindow::controlAt(const QPoint& point) const {
    if (!maskContains(m_skin != nullptr ? m_skin->region : skins::Region{}, m_state.shade,
                      QPoint(point.x() / m_scale, point.y() / m_scale))) {
        return HitId::None;
    }
    return hitTest(m_state, point, m_scale);
}

bool MainWindow::isTitleBarAt(const QPoint& point) const { return controlAt(point) == HitId::TitleBar; }

void MainWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const HitId id = controlAt(event->pos());
    if (id == HitId::TitleBar) {
        SkinnedWindow::mousePressEvent(event);  // start the window drag
        return;
    }
    pressControl(id, event->pos());
    event->accept();
}

void MainWindow::mouseMoveEvent(QMouseEvent* event) {
    if (isDragging()) {
        SkinnedWindow::mouseMoveEvent(event);
        return;
    }
    if (m_dragging != HitId::None) {
        dragControl(m_dragging, event->pos());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        SkinnedWindow::mouseReleaseEvent(event);
        return;
    }
    if (isDragging()) {
        SkinnedWindow::mouseReleaseEvent(event);
        return;
    }
    const HitId pressed = m_pressed;
    m_pressed = HitId::None;
    const HitId dragged = m_dragging;
    m_dragging = HitId::None;
    if (dragged != HitId::None) {
        releaseControl(dragged, event->pos());
    } else if (pressed != HitId::None && controlAt(event->pos()) == pressed) {
        activate(pressed);
    }
    event->accept();
}

void MainWindow::wheelEvent(QWheelEvent* event) {
    const QPoint point = event->position().toPoint();
    const HitId id = controlAt(point);
    const int steps = event->angleDelta().y() / 120;
    if (id == HitId::Volume) {
        const int volume = std::clamp(m_state.volume + steps * 2, 0, 100);
        if (m_transport != nullptr) {
            m_transport->setVolumePercent(volume);
        }
        m_state.volume = volume;
        requestRepaint();
        event->accept();
        return;
    }
    if (id == HitId::Balance) {
        // Wheel steps are discrete, so they bypass the centre detent (which is
        // a pointing aid) — otherwise the wheel could never leave the centre.
        m_state.balance = std::clamp(m_state.balance + steps * 2, -100, 100);
        requestRepaint();
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Z:
            if (m_transport != nullptr) {
                m_transport->previous();
            }
            break;
        case Qt::Key_X:
            playCurrent();
            break;
        case Qt::Key_C:
            if (m_transport != nullptr) {
                m_transport->pause();
            }
            break;
        case Qt::Key_V:
            if (m_transport != nullptr) {
                m_transport->stop();
            }
            break;
        case Qt::Key_B:
            if (m_transport != nullptr) {
                m_transport->next();
            }
            break;
        case Qt::Key_L:
            openFiles();
            break;
        case Qt::Key_S:
            toggleShuffle();
            break;
        case Qt::Key_R:
            toggleRepeat();
            break;
        case Qt::Key_Left:
            seekRelative(-5);
            break;
        case Qt::Key_Right:
            seekRelative(5);
            break;
        case Qt::Key_Up:
            if (m_transport != nullptr) {
                m_transport->setVolumePercent(std::clamp(m_state.volume + 1, 0, 100));
            }
            break;
        case Qt::Key_Down:
            if (m_transport != nullptr) {
                m_transport->setVolumePercent(std::clamp(m_state.volume - 1, 0, 100));
            }
            break;
        default:
            QWidget::keyPressEvent(event);
            return;
    }
    syncFromTransport();
    requestRepaint();
    event->accept();
}

void MainWindow::pressControl(HitId id, const QPoint& point) {
    m_pressed = id;
    switch (id) {
        case HitId::Play:
            m_state.active.play = true;
            break;
        case HitId::Pause:
            m_state.active.pause = true;
            break;
        case HitId::Stop:
            m_state.active.stop = true;
            break;
        case HitId::Previous:
            m_state.active.previous = true;
            break;
        case HitId::Next:
            m_state.active.next = true;
            break;
        case HitId::Eject:
            m_state.active.eject = true;
            break;
        case HitId::Shuffle:
            m_state.shuffleDepressed = true;
            break;
        case HitId::Repeat:
            m_state.repeatDepressed = true;
            break;
        case HitId::EqButton:
            m_state.eqDepressed = true;
            break;
        case HitId::PlButton:
            m_state.plDepressed = true;
            break;
        case HitId::OptionButton:
            m_state.pressed.option = true;
            break;
        case HitId::MinimizeButton:
            m_state.pressed.minimize = true;
            break;
        case HitId::ShadeButton:
            m_state.pressed.shade = true;
            break;
        case HitId::CloseButton:
            m_state.pressed.close = true;
            break;
        case HitId::Volume:
            m_dragging = HitId::Volume;
            m_state.volumeActive = true;
            setVolumeFromPoint(point.x());
            break;
        case HitId::Balance:
            m_dragging = HitId::Balance;
            m_state.balanceActive = true;
            setBalanceFromPoint(point.x());
            break;
        case HitId::Position:
            m_dragging = HitId::Position;
            m_state.positionActive = true;
            seekPercentFromPoint(point.x(), false);
            break;
        case HitId::ShadePosition:
            m_dragging = HitId::ShadePosition;
            m_state.positionActive = true;
            seekPercentFromPoint(point.x(), true);
            break;
        default:
            break;
    }
    requestRepaint();
}

void MainWindow::dragControl(HitId id, const QPoint& point) {
    switch (id) {
        case HitId::Volume:
            setVolumeFromPoint(point.x());
            break;
        case HitId::Balance:
            setBalanceFromPoint(point.x());
            break;
        case HitId::Position:
            seekPercentFromPoint(point.x(), false);
            break;
        case HitId::ShadePosition:
            seekPercentFromPoint(point.x(), true);
            break;
        default:
            return;
    }
    requestRepaint();
}

void MainWindow::releaseControl(HitId id, const QPoint& point) {
    switch (id) {
        case HitId::Volume:
            m_state.volumeActive = false;
            setVolumeFromPoint(point.x());
            break;
        case HitId::Balance:
            m_state.balanceActive = false;
            setBalanceFromPoint(point.x());
            break;
        case HitId::Position:
            m_state.positionActive = false;
            seekPercentFromPoint(point.x(), false);
            break;
        case HitId::ShadePosition:
            m_state.positionActive = false;
            seekPercentFromPoint(point.x(), true);
            break;
        default:
            // A release outside the control cancels without activating.
            m_state.active = ActiveButtons{};
            m_state.pressed = WindowButtons{};
            m_state.shuffleDepressed = false;
            m_state.repeatDepressed = false;
            m_state.eqDepressed = false;
            m_state.plDepressed = false;
            break;
    }
    requestRepaint();
}

void MainWindow::activate(HitId id) {
    switch (id) {
        case HitId::Play:
            m_state.active.play = false;
            playCurrent();
            break;
        case HitId::Pause:
            m_state.active.pause = false;
            if (m_transport != nullptr) {
                m_transport->togglePause();
            }
            break;
        case HitId::Stop:
            m_state.active.stop = false;
            if (m_transport != nullptr) {
                m_transport->stop();
                m_transport->seekMs(0);
            }
            break;
        case HitId::Previous:
            m_state.active.previous = false;
            if (m_transport != nullptr) {
                m_transport->previous();
            }
            break;
        case HitId::Next:
            m_state.active.next = false;
            if (m_transport != nullptr) {
                m_transport->next();
            }
            break;
        case HitId::Eject:
            m_state.active.eject = false;
            openFiles();
            break;
        case HitId::Shuffle:
            m_state.shuffleDepressed = false;
            toggleShuffle();
            break;
        case HitId::Repeat:
            m_state.repeatDepressed = false;
            toggleRepeat();
            break;
        case HitId::EqButton:
            m_state.eqDepressed = false;
            m_state.eqSelected = !m_state.eqSelected;
            emit eqToggled(m_state.eqSelected);
            break;
        case HitId::PlButton:
            m_state.plDepressed = false;
            m_state.plSelected = !m_state.plSelected;
            emit playlistToggled(m_state.plSelected);
            break;
        case HitId::OptionButton:
            m_state.pressed.option = false;
            emit menuRequested();
            break;
        case HitId::MinimizeButton:
            m_state.pressed.minimize = false;
            showMinimized();
            break;
        case HitId::ShadeButton:
            m_state.pressed.shade = false;
            setShade(!m_state.shade);
            break;
        case HitId::CloseButton:
            m_state.pressed.close = false;
            emit closeRequested();
            break;
        case HitId::Time:
        case HitId::ShadeMiniTime:
            toggleTimeMode();
            break;
        case HitId::Visualizer:
            cycleVisualizer();
            break;
        case HitId::ClutterO:
            m_state.clutter.o = !m_state.clutter.o;
            emit menuRequested();
            break;
        case HitId::ClutterA:
            m_state.clutter.a = !m_state.clutter.a;
            setAlwaysOnTop(m_state.clutter.a);
            break;
        case HitId::ClutterI:
            m_state.clutter.i = !m_state.clutter.i;
            emit fileInfoRequested();
            break;
        case HitId::ClutterD:
            setDoubleSize(!m_state.clutter.d);
            break;
        case HitId::ClutterV:
            m_state.clutter.v = !m_state.clutter.v;
            emit visualizerToggled(m_state.clutter.v);
            break;
        default:
            break;
    }
    syncFromTransport();
    requestRepaint();
}

void MainWindow::toggleShuffle() {
    if (m_transport != nullptr) {
        m_transport->setShuffle(!m_state.shuffle);
        m_state.shuffle = m_transport->shuffle();
    } else {
        m_state.shuffle = !m_state.shuffle;
    }
}

void MainWindow::toggleRepeat() {
    if (m_transport != nullptr) {
        // Winamp's repeat button cycles: off -> repeat all -> off (repeat-one
        // is a Winamp 5 addition; RepeatMode::One stays reachable through the
        // transport API and renders selected).
        const playback::RepeatMode next = m_state.repeat ? playback::RepeatMode::Off
                                                         : playback::RepeatMode::All;
        m_transport->setRepeatMode(next);
        m_state.repeat = m_transport->repeatMode() != playback::RepeatMode::Off;
    } else {
        m_state.repeat = !m_state.repeat;
    }
}

void MainWindow::toggleTimeMode() {
    m_state.countdown = !m_state.countdown;
    syncFromTransport();
    requestRepaint();
}

void MainWindow::cycleVisualizer() {
    switch (m_state.vis) {
        case VisMode::Spectrum:
            m_state.vis = VisMode::Oscilloscope;
            break;
        case VisMode::Oscilloscope:
            m_state.vis = VisMode::Off;
            break;
        case VisMode::Off:
            m_state.vis = VisMode::Spectrum;
            break;
    }
    requestRepaint();
    updateTickTimer();
}

void MainWindow::seekRelative(int seconds) {
    if (m_transport == nullptr) {
        return;
    }
    const std::int64_t length = m_transport->lengthMs();
    std::int64_t position = m_transport->positionMs() + seconds * 1000;
    position = std::max<std::int64_t>(0, position);
    if (length > 0) {
        position = std::min(position, length);
    }
    m_transport->seekMs(position);
}

void MainWindow::seekPercentFromPoint(int localX, bool shadePosition) {
    const QRect track = shadePosition ? S::position : L::position;
    const int thumb = shadePosition ? SliderMetrics::shadeThumbW : SliderMetrics::positionThumbW;
    const int percent = valueFromX(localX / m_scale, track.x(), track.width(), thumb, 0, 100);
    m_state.position = percent;
    if (m_transport != nullptr) {
        const std::int64_t length = m_transport->lengthMs();
        if (length > 0) {
            m_transport->seekMs(static_cast<std::int64_t>(std::lround(
                static_cast<double>(percent) / 100.0 * static_cast<double>(length))));
        }
    } else {
        m_state.timeSeconds = 0;
    }
}

void MainWindow::setVolumeFromPoint(int localX) {
    const int volume =
        valueFromX(localX / m_scale, L::volume.x(), SliderMetrics::volumeTrackW,
                   SliderMetrics::thumbW, 0, 100);
    m_state.volume = volume;
    if (m_transport != nullptr) {
        m_transport->setVolumePercent(volume);
    }
}

void MainWindow::setBalanceFromPoint(int localX) {
    const int x = localX / m_scale;
    const int span = SliderMetrics::balanceTrackW - SliderMetrics::thumbW;
    // The centre detent is measured on the thumb's position, not on the pan
    // value: one pixel is ~8 pan units, so a value-domain threshold would be
    // unreachable by pointing.
    const bool centred = std::abs((x - L::balance.x()) - span / 2) <= kBalanceCentreDetent;
    m_state.balance = centred ? 0
                              : valueFromX(x, L::balance.x(), SliderMetrics::balanceTrackW,
                                           SliderMetrics::thumbW, -100, 100);
}

void MainWindow::openFiles() {
    emit ejectRequested();
    if (!m_fileDialogEnabled) {
        return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Open media"), QString(), tr(kAudioFilter));
    if (files.isEmpty()) {
        return;
    }
    if (m_transport == nullptr) {
        return;
    }
    playback::MediaRef media;
    media.kind = playback::MediaRef::Kind::Path;
    media.value = files.first().toStdString();
    m_currentMedia = std::make_unique<playback::MediaRef>(media);
    m_transport->playMedia(media);
    for (int i = 1; i < files.size(); ++i) {
        playback::MediaRef extra;
        extra.kind = playback::MediaRef::Kind::Path;
        extra.value = files[static_cast<std::size_t>(i)].toStdString();
        m_transport->playMedia(extra);  // the engine appends to the session list
    }
    syncFromTransport();
    requestRepaint();
}

void MainWindow::playCurrent() {
    if (m_transport == nullptr) {
        emit mediaOpenRequested();
        return;
    }
    if (m_state.status == PlayerStatus::Stopped) {
        if (m_currentMedia != nullptr) {
            m_transport->playMedia(*m_currentMedia);
        } else if (m_transport->hasMedia()) {
            m_transport->resume();  // a media is loaded but stopped: play it
        } else {
            openFiles();
            return;
        }
    } else if (m_state.status == PlayerStatus::Paused) {
        m_transport->togglePause();
    } else {
        m_transport->seekMs(0);  // Winamp's play button restarts the track
    }
    syncFromTransport();
    requestRepaint();
}

}  // namespace koalaamp::ui
