// SPDX-License-Identifier: MIT
// VisualizerControls — see the header for the genex/fallback contract.

#include "ui/VisualizerControls.hpp"

#include "skins/SkinSet.hpp"
#include "ui/GenexStyle.hpp"

#include <QHBoxLayout>

namespace koalaamp::ui {
namespace {

constexpr int kButtonHeight = 22;
constexpr int kGap = 3;
constexpr int kMargin = 4;
constexpr int kTextPixelSize = 11;

} // namespace

VisualizerControls::VisualizerControls(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("visualizerControls"));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(kGap);

    const auto add = [this](const QString& text, const QString& name, int width) {
        auto* button = new GenexButton(text, this);
        button->setObjectName(name);
        button->setTextPixelSize(kTextPixelSize);
        button->setFixedSize(width, kButtonHeight);
        m_layout->addWidget(button);
        return button;
    };
    m_previous = add(QStringLiteral("PREV"), QStringLiteral("visualizerPrevious"), 34);
    m_next = add(QStringLiteral("NEXT"), QStringLiteral("visualizerNext"), 34);
    m_order = add(QStringLiteral("ORDER"), QStringLiteral("visualizerOrder"), 56);
    m_lock = add(QStringLiteral("LOCK"), QStringLiteral("visualizerLock"), 44);
    m_fullscreenButton = add(QStringLiteral("FULL"), QStringLiteral("visualizerFullscreen"), 44);
    m_order->setCheckable(true);
    m_lock->setCheckable(true);
    m_fullscreenButton->setCheckable(true);

    QObject::connect(m_previous, &QPushButton::clicked, this,
                     &VisualizerControls::previousRequested);
    QObject::connect(m_next, &QPushButton::clicked, this, &VisualizerControls::nextRequested);
    QObject::connect(m_order, &QPushButton::clicked, this,
                     &VisualizerControls::orderToggleRequested);
    QObject::connect(m_lock, &QPushButton::clicked, this,
                     &VisualizerControls::lockedToggleRequested);
    QObject::connect(m_fullscreenButton, &QPushButton::clicked, this,
                     &VisualizerControls::fullscreenToggleRequested);
    applyScale();
}

void VisualizerControls::setSkin(const skins::SkinSet* skin) {
    const skins::Skin* sheets = skin != nullptr ? &skin->sheets : nullptr;
    for (GenexButton* button : {m_previous, m_next, m_order, m_lock, m_fullscreenButton}) {
        button->setSkin(sheets);
    }
    update();
}

void VisualizerControls::setScale(int scale) {
    const int clamped = qBound(1, scale, 4);
    if (clamped == m_scale) {
        return;
    }
    m_scale = clamped;
    applyScale();
    updateGeometry();
}

void VisualizerControls::applyScale() {
    m_layout->setContentsMargins(kMargin * m_scale, kMargin * m_scale, kMargin * m_scale,
                                 kMargin * m_scale);
    m_layout->setSpacing(kGap * m_scale);
    const struct {
        GenexButton* button;
        int width;
    } widths[] = {
        {m_previous, 34}, {m_next, 34}, {m_order, 56}, {m_lock, 44}, {m_fullscreenButton, 44},
    };
    for (const auto& entry : widths) {
        entry.button->setTextPixelSize(kTextPixelSize * m_scale);
        entry.button->setFixedSize(entry.width * m_scale, kButtonHeight * m_scale);
    }
    setFixedSize(sizeHint());
}

void VisualizerControls::setState(bool random, bool locked, bool fullscreen) {
    m_random = random;
    m_locked = locked;
    m_fullscreen = fullscreen;
    m_order->setChecked(random);
    m_order->setText(random ? QStringLiteral("RANDOM") : QStringLiteral("ORDER"));
    m_lock->setChecked(locked);
    m_fullscreenButton->setChecked(fullscreen);
    update();
}

QSize VisualizerControls::sizeHint() const {
    return QSize((34 + 34 + 56 + 44 + 44) * m_scale + (kGap * 4 + kMargin * 2) * m_scale,
                 (kButtonHeight + kMargin * 2) * m_scale);
}

} // namespace koalaamp::ui
