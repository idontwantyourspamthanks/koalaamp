// SPDX-License-Identifier: MIT
#include "ui/EqWindow.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#include <QApplication>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "ui/EqRenderer.hpp"
#include "ui/SkinHitRegion.hpp"

namespace koalaamp::ui {

namespace {

using audio::EqMode;
using audio::EqPreset;
using audio::EqSettings;

/// Slider body height used by the drag maths (Webamp VerticalSlider).
constexpr int kTrackSpan = EqSliderMetrics::trackH - EqSliderMetrics::thumbH;  // 51

int clampSlider(int value) { return std::clamp(value, kEqValueBoost, kEqValueCut); }

/// Slider value from a track-local x (Webamp `thumbLeft` inverted).
int valueFromX(int localX, int trackLeft, int trackWidth, int thumbWidth, int minValue,
               int maxValue) {
    const int span = std::max(1, trackWidth - thumbWidth);
    const double fraction =
        std::clamp(static_cast<double>(localX - trackLeft) / static_cast<double>(span), 0.0, 1.0);
    return minValue + static_cast<int>(std::lround(fraction * (maxValue - minValue)));
}

EqPreset presetFromState(const EqWindowState& state, std::string name = {}) {
    EqPreset preset;
    preset.name = std::move(name);
    preset.preamp = state.sliders[0];
    for (int i = 0; i < kEqWindowBands; ++i) {
        preset.bands[static_cast<std::size_t>(i)] = state.sliders[static_cast<std::size_t>(i + 1)];
    }
    return preset;
}

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

const std::vector<std::string>& checklistStorage() {
    static const std::vector<std::string> items = {
        // Window chrome
        "titlebar-drag-moves-window",
        "titlebar-double-click-toggles-shade",
        "shade-button-toggles-shade",
        "shade-uses-275x14-geometry",
        "close-click-requests-close",
        "region-mask-applied-in-both-states",
        "double-size-is-integer-nearest-neighbour",
        // Buttons
        "on-click-toggles-enabled",
        "on-bypass-passes-enabled-false",
        "auto-click-toggles-autoload",
        "presets-button-renders-two-states",
        // Sliders
        "preamp-drag-sets-value",
        "band-drag-sets-value",
        "slider-thumb-selected-while-dragging",
        "slider-wheel-steps-value",
        "slider-keyboard-steps-value",
        // dB zones / reset
        "plus12-zone-sets-bands-max",
        "zerodb-zone-sets-all-unity",
        "minus12-zone-sets-bands-min",
        // Presets
        "classic-preset-loads-sliders",
        "eqf-save-load-round-trip",
        "preset-change-marks-custom",
        // Shade previews
        "shade-volume-drag-updates-preview",
        "shade-balance-drag-updates-preview",
        // DSP wiring
        "slider-pushes-eqsettings",
        "preset-load-pushes-eqsettings",
    };
    return items;
}

}  // namespace

const std::vector<std::string>& EqWindow::interactionChecklist() { return checklistStorage(); }

EqWindow::EqWindow(QWidget* parent) : SkinnedWindow(parent) {
    setWindowTitle(QStringLiteral("KoalaAmp Equalizer"));
    updateShape();
    requestRepaint();
    connect(this, &SkinnedWindow::shadeToggleRequested, this, [this] { setShade(!m_state.shade); });
}

EqWindow::~EqWindow() = default;

// ---------------------------------------------------------------------------
// Skin / geometry
// ---------------------------------------------------------------------------

void EqWindow::setSkin(const skins::SkinSet* skin) {
    m_skin = skin;
    updateShape();
    requestRepaint();
}

void EqWindow::updateShape() {
    const QSize content(kWidth * m_scale, (m_state.shade ? kShadeHeight : kHeight) * m_scale);
    static const skins::Region none;
    const skins::Region& region = m_skin != nullptr ? m_skin->region : none;
    setWindowShape(region, m_state.shade, content, m_scale);
    requestRepaint();
}

void EqWindow::requestRepaint() {
    setFixedSize(kWidth * m_scale, (m_state.shade ? kShadeHeight : kHeight) * m_scale);
    update();
}

void EqWindow::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (m_skin != nullptr) {
        painter.drawImage(0, 0, renderEqWindow(m_skin->sheets, m_state, m_scale));
    } else {
        painter.fillRect(rect(), Qt::black);
    }
}

void EqWindow::setScale(int scale) {
    const int clamped = std::clamp(scale, 1, 4);
    if (clamped == m_scale) {
        return;
    }
    m_scale = clamped;
    updateShape();
    emit doubleSizeChanged(m_scale > 1);
}

void EqWindow::setDoubleSize(bool on) { setScale(on ? 2 : 1); }

void EqWindow::setShade(bool shaded) {
    if (shaded == m_state.shade) {
        return;
    }
    m_state.shade = shaded;
    updateShape();
    emit shadeChanged(shaded);
}

void EqWindow::setAlwaysOnTop(bool on) {
    const QPoint position = pos();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    show();
    move(position);
    requestRepaint();
    emit alwaysOnTopChanged(on);
}

bool EqWindow::isAlwaysOnTop() const {
    return (windowFlags() & Qt::WindowStaysOnTopHint) != 0;
}

void EqWindow::setVolumeBalance(int volume, int balance) {
    m_state.volume = std::clamp(volume, 0, 100);
    m_state.balance = std::clamp(balance, -100, 100);
    requestRepaint();
}

// ---------------------------------------------------------------------------
// Audio seam / model
// ---------------------------------------------------------------------------

void EqWindow::setEqSink(IEqSink* sink) {
    m_sink = sink;
    pushSettings();
}

void EqWindow::setAlbumEqStore(audio::AlbumEqStore* store) {
    m_albumStore = store;
    if (store != nullptr) {
        store->setIndicatorHook([this](bool active, const EqPreset& preset) {
            m_state.autoLoad = active;
            (void)preset;
            requestRepaint();
        });
        m_state.autoLoad = store->enabled();
    }
}

void EqWindow::setState(const EqWindowState& state) {
    m_state = state;
    updateShape();
    pushSettings();
}

audio::EqSettings EqWindow::settingsFromState() const {
    EqSettings settings;
    settings.enabled = m_state.enabled;
    settings.mode = m_mode;
    settings.preamp = m_state.sliders[0];
    for (int i = 0; i < kEqWindowBands; ++i) {
        settings.bands[static_cast<std::size_t>(i)] = m_state.sliders[static_cast<std::size_t>(i + 1)];
    }
    return settings;
}

void EqWindow::pushSettings() {
    if (m_sink != nullptr) {
        m_sink->setEqSettings(settingsFromState());
    }
}

int EqWindow::sliderValue(int index) const {
    if (index < 0 || index >= kEqWindowSliders) {
        return kEqValueUnity;
    }
    return m_state.sliders[static_cast<std::size_t>(index)];
}

void EqWindow::setSliderValue(int index, int auValue) {
    if (index < 0 || index >= kEqWindowSliders) {
        return;
    }
    const int clamped = clampSlider(auValue);
    if (m_state.sliders[static_cast<std::size_t>(index)] == clamped) {
        return;
    }
    m_state.sliders[static_cast<std::size_t>(index)] = clamped;
    m_state.presetIndex = -1;  // a manual edit makes the settings custom
    m_focusSlider = index;
    pushSettings();
    requestRepaint();
}

void EqWindow::resetAll() {
    EqWindowState next = m_state;
    next.sliders.fill(kEqValueUnity);
    next.presetIndex = -1;
    setState(next);
}

void EqWindow::setAllBands(int auValue) {
    EqWindowState next = m_state;
    for (int i = 1; i < kEqWindowSliders; ++i) {
        next.sliders[static_cast<std::size_t>(i)] = clampSlider(auValue);
    }
    next.presetIndex = -1;
    setState(next);
}

void EqWindow::applyEqSettings(const EqSettings& settings, int presetIndex) {
    m_mode = settings.mode;
    m_state.enabled = settings.enabled;
    m_state.sliders[0] = clampSlider(settings.preamp);
    for (int i = 0; i < kEqWindowBands; ++i) {
        m_state.sliders[static_cast<std::size_t>(i + 1)] =
            clampSlider(settings.bands[static_cast<std::size_t>(i)]);
    }
    m_state.presetIndex = presetIndex;
    pushSettings();
    requestRepaint();
}

void EqWindow::applyPreset(const EqPreset& preset, int presetIndex) {
    EqSettings settings;
    settings.enabled = m_state.enabled;
    settings.mode = m_mode;
    settings.preamp = clampSlider(preset.preamp);
    for (int i = 0; i < kEqWindowBands; ++i) {
        settings.bands[static_cast<std::size_t>(i)] =
            clampSlider(preset.bands[static_cast<std::size_t>(i)]);
    }
    applyEqSettings(settings, presetIndex);
}

bool EqWindow::loadClassicPreset(int index) {
    const auto& presets = audio::classicEqPresets();
    if (index < 0 || static_cast<std::size_t>(index) >= presets.size()) {
        return false;
    }
    applyPreset(presets[static_cast<std::size_t>(index)], index);
    emit presetSelected(index);
    return true;
}

const std::vector<EqPreset>& EqWindow::classicPresets() const { return audio::classicEqPresets(); }

int EqWindow::importPresetsFromBytes(const std::vector<std::uint8_t>& bytes) {
    std::vector<EqPreset> parsed = audio::parseEqf(bytes);
    const int count = static_cast<int>(parsed.size());
    for (EqPreset& preset : parsed) {
        m_userPresets.push_back(std::move(preset));
    }
    return count;
}

bool EqWindow::savePresetToFile(const std::string& path) const {
    const EqPreset preset = presetFromState(m_state, "KoalaAmp EQ");
    const std::vector<std::uint8_t> bytes = audio::writeEqf(preset);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

bool EqWindow::loadPresetFromFile(const std::string& path) {
    const std::vector<std::uint8_t> bytes = readFile(path);
    const std::vector<EqPreset> parsed = audio::parseEqf(bytes);
    if (parsed.empty()) {
        return false;
    }
    applyPreset(parsed.front(), -1);
    return true;
}

bool EqWindow::importPresetsFromFile(const std::string& path) {
    const std::vector<std::uint8_t> bytes = readFile(path);
    return importPresetsFromBytes(bytes) > 0;
}

bool EqWindow::notifyAlbumChanged(const std::string& albumKey) {
    if (m_albumStore == nullptr) {
        return false;
    }
    const std::optional<EqPreset> applied = m_albumStore->onAlbumChanged(albumKey);
    if (!applied.has_value()) {
        return false;
    }
    applyPreset(*applied, -1);
    return true;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void EqWindow::setSliderFromPoint(int index, const QPoint& point, bool preserveGrab) {
    const int y = point.y() / m_scale - EqLayout::bandY;
    if (!preserveGrab) {
        const int thumbTop = eqThumbTop(eqWebampValue(sliderValue(index)));
        m_grabOffset = (y >= thumbTop && y < thumbTop + EqSliderMetrics::thumbH)
                           ? y - thumbTop
                           : EqSliderMetrics::thumbH / 2;
    }
    const double fraction =
        std::clamp(static_cast<double>(y - m_grabOffset) / kTrackSpan, 0.0, 1.0);
    const int webamp = static_cast<int>(std::lround((1.0 - fraction) * 100.0));
    setSliderValue(index, eqAuValueFromWebamp(webamp));
}

bool EqWindow::isTitleBarAt(const QPoint& point) const {
    if (!maskContains(m_skin != nullptr ? m_skin->region : skins::Region{}, m_state.shade,
                      QPoint(point.x() / m_scale, point.y() / m_scale))) {
        return false;
    }
    return eqHitTest(m_state, point, m_scale) == EqHitId::TitleBar;
}

void EqWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (!maskContains(m_skin != nullptr ? m_skin->region : skins::Region{}, m_state.shade,
                      QPoint(event->pos().x() / m_scale, event->pos().y() / m_scale))) {
        event->accept();
        return;
    }
    int sliderIndex = -1;
    const EqHitId id = eqHitTest(m_state, event->pos(), m_scale, &sliderIndex);
    if (id == EqHitId::TitleBar) {
        SkinnedWindow::mousePressEvent(event);
        return;
    }
    switch (id) {
        case EqHitId::ShadeButton:
            m_state.shadePressed = true;
            break;
        case EqHitId::CloseButton:
            m_state.closePressed = true;
            break;
        case EqHitId::OnButton:
            m_state.onPressed = true;
            break;
        case EqHitId::AutoButton:
            m_state.autoPressed = true;
            break;
        case EqHitId::PresetsButton:
            m_state.presetsPressed = true;
            break;
        case EqHitId::Slider:
            m_dragging = EqHitId::Slider;
            m_dragSlider = sliderIndex;
            m_state.activeSlider = sliderIndex;
            setSliderFromPoint(sliderIndex, event->pos(), false);
            break;
        case EqHitId::ShadeVolume:
            m_dragging = EqHitId::ShadeVolume;
            m_state.volume =
                valueFromX(event->pos().x() / m_scale, EqLayout::shadeVolume.x(),
                           EqLayout::shadeVolume.width(), EqSliderMetrics::shadeThumbW, 0, 100);
            break;
        case EqHitId::ShadeBalance: {
            m_dragging = EqHitId::ShadeBalance;
            const int x = event->pos().x() / m_scale;
            const int span = EqLayout::shadeBalance.width() - EqSliderMetrics::shadeThumbW;
            const double fraction = std::clamp(
                static_cast<double>(x - EqLayout::shadeBalance.x()) / span, 0.0, 1.0);
            m_state.balance = static_cast<int>(std::lround(fraction * 200.0)) - 100;
            break;
        }
        default:
            break;
    }
    requestRepaint();
    event->accept();
}

void EqWindow::mouseMoveEvent(QMouseEvent* event) {
    if (isDragging()) {
        SkinnedWindow::mouseMoveEvent(event);
        return;
    }
    if (m_dragging == EqHitId::Slider) {
        setSliderFromPoint(m_dragSlider, event->pos(), true);
        event->accept();
        return;
    }
    if (m_dragging == EqHitId::ShadeVolume) {
        m_state.volume =
            valueFromX(event->pos().x() / m_scale, EqLayout::shadeVolume.x(),
                       EqLayout::shadeVolume.width(), EqSliderMetrics::shadeThumbW, 0, 100);
        requestRepaint();
        event->accept();
        return;
    }
    if (m_dragging == EqHitId::ShadeBalance) {
        const int x = event->pos().x() / m_scale;
        const int span = EqLayout::shadeBalance.width() - EqSliderMetrics::shadeThumbW;
        const double fraction =
            std::clamp(static_cast<double>(x - EqLayout::shadeBalance.x()) / span, 0.0, 1.0);
        m_state.balance = static_cast<int>(std::lround(fraction * 200.0)) - 100;
        requestRepaint();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void EqWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        SkinnedWindow::mouseReleaseEvent(event);
        return;
    }
    if (isDragging()) {
        SkinnedWindow::mouseReleaseEvent(event);
        return;
    }
    if (m_dragging == EqHitId::Slider) {
        m_dragging = EqHitId::None;
        m_state.activeSlider = -1;
        requestRepaint();
        event->accept();
        return;
    }
    if (m_dragging != EqHitId::None) {
        m_dragging = EqHitId::None;
        event->accept();
        return;
    }

    int sliderIndex = -1;
    const EqHitId id = eqHitTest(m_state, event->pos(), m_scale, &sliderIndex);
    m_state.shadePressed = false;
    m_state.closePressed = false;
    m_state.onPressed = false;
    m_state.autoPressed = false;
    m_state.presetsPressed = false;
    activate(id, sliderIndex);
    requestRepaint();
    event->accept();
}

void EqWindow::activate(EqHitId id, int sliderIndex) {
    (void)sliderIndex;
    switch (id) {
        case EqHitId::ShadeButton:
            setShade(!m_state.shade);
            break;
        case EqHitId::CloseButton:
            emit closeRequested();
            break;
        case EqHitId::OnButton: {
            m_state.enabled = !m_state.enabled;
            pushSettings();
            emit eqEnabledChanged(m_state.enabled);
            break;
        }
        case EqHitId::AutoButton: {
            m_state.autoLoad = !m_state.autoLoad;
            if (m_albumStore != nullptr) {
                m_albumStore->setEnabled(m_state.autoLoad);
            }
            emit autoLoadChanged(m_state.autoLoad);
            break;
        }
        case EqHitId::PresetsButton:
            if (m_presetDialogsEnabled) {
                showPresetsMenu();
            }
            break;
        case EqHitId::SetMaxDb:
            setAllBands(kEqValueBoost);
            break;
        case EqHitId::SetUnityDb:
            setAllBands(kEqValueUnity);
            break;
        case EqHitId::SetMinDb:
            setAllBands(kEqValueCut);
            break;
        default:
            break;
    }
}

void EqWindow::wheelEvent(QWheelEvent* event) {
    int sliderIndex = -1;
    const EqHitId id = eqHitTest(m_state, event->position().toPoint(), m_scale, &sliderIndex);
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0) {
        QWidget::wheelEvent(event);
        return;
    }
    if (id == EqHitId::Slider) {
        setSliderValue(sliderIndex, sliderValue(sliderIndex) - steps);
        event->accept();
        return;
    }
    if (id == EqHitId::ShadeVolume) {
        m_state.volume = std::clamp(m_state.volume + steps * 2, 0, 100);
        requestRepaint();
        event->accept();
        return;
    }
    if (id == EqHitId::ShadeBalance) {
        m_state.balance = std::clamp(m_state.balance + steps * 2, -100, 100);
        requestRepaint();
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void EqWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Up:
            setSliderValue(m_focusSlider, sliderValue(m_focusSlider) - 1);
            break;
        case Qt::Key_Down:
            setSliderValue(m_focusSlider, sliderValue(m_focusSlider) + 1);
            break;
        default:
            QWidget::keyPressEvent(event);
            return;
    }
    event->accept();
}

// ---------------------------------------------------------------------------
// Preset menu / dialogs
// ---------------------------------------------------------------------------

void EqWindow::showPresetsMenu() {
    QMenu menu(this);
    QMenu* load = menu.addMenu(tr("Load"));
    int index = 0;
    for (const EqPreset& preset : audio::classicEqPresets()) {
        const QString name = QString::fromStdString(preset.name);
        load->addAction(name, this, [this, index] { loadClassicPreset(index); });
        ++index;
    }
    for (const EqPreset& preset : m_userPresets) {
        const EqPreset copy = preset;
        load->addAction(QString::fromStdString(preset.name), this,
                        [this, copy] { applyPreset(copy, -1); });
    }
    load->addSeparator();
    load->addAction(tr("From .eqf..."), this, [this] { openPresetDialog(); });
    menu.addAction(tr("Save..."), this, [this] { savePresetDialog(); });
    menu.exec(mapToGlobal(QPoint(EqLayout::presetsButton.x() * m_scale,
                                 EqLayout::presetsButton.bottom() * m_scale)));
}

void EqWindow::openPresetDialog() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load EQ preset"), QString(), tr("Winamp EQ presets (*.eqf *.q1 *.q2);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (!loadPresetFromFile(path.toStdString())) {
        importPresetsFromFile(path.toStdString());
    }
}

void EqWindow::savePresetDialog() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Save EQ preset"), QString(),
                                                      tr("Winamp EQ preset (*.eqf)"));
    if (!path.isEmpty()) {
        savePresetToFile(path.toStdString());
    }
}

}  // namespace koalaamp::ui
