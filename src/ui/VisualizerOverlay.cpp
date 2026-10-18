// SPDX-License-Identifier: MIT
//
// VisualizerOverlay — see VisualizerOverlay.hpp for why it is a sibling widget.
#include "ui/VisualizerOverlay.hpp"

#include <algorithm>
#include <cstddef>

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPainter>

#include "skins/SkinSet.hpp"
#include "skins/SkinSprites.hpp"
#include "ui/TitleText.hpp"

namespace koalaamp::ui {
namespace {

constexpr char kSheetText[] = "TEXT";
/// Padding around the name, in 1x skin pixels.
constexpr int kPlatePaddingX = 6;
constexpr int kPlatePaddingY = 4;
/// The name sits in the upper third of the render.
constexpr int kVerticalDivisor = 3;

}  // namespace

VisualizerOverlay::VisualizerOverlay(QWidget* parent) : QWidget(parent) {
    // The overlay must never eat the window's input, and must let the render show
    // through everywhere it does not paint.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    m_timer.setInterval(kTickMs);
    QObject::connect(&m_timer, &QTimer::timeout, this, [this] { tick(); });
    hide();
}

void VisualizerOverlay::setSkin(const skins::SkinSet* skin) {
    m_skin = skin;
    if (showing()) {
        update();
    }
}

void VisualizerOverlay::setScale(int scale) {
    const int clamped = std::clamp(scale, 1, 4);
    if (clamped == m_scale) {
        return;
    }
    m_scale = clamped;
    if (showing()) {
        update();
    }
}

void VisualizerOverlay::showName(const std::string& name) {
    m_name = name;
    if (m_name.empty()) {
        m_timer.stop();
        m_opacity = 0.0;
        hide();
        return;
    }
    m_opacity = 1.0;
    // Start the clock now (an offscreen window never maps, and the name must still
    // expire) — and again in showEvent, so a name announced before the window is
    // mapped does not lose its time to the wait.
    m_since.restart();
    m_timer.start();
    show();
    raise();
    update();
}

void VisualizerOverlay::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    m_timer.stop();
    m_opacity = 0.0;
}

void VisualizerOverlay::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // Kick the fade clock only now: hiding/showing must not eat the name's time.
    m_since.restart();
    m_timer.start();
}

void VisualizerOverlay::tick() {
    const qint64 elapsed = m_since.elapsed();
    if (elapsed >= kVisibleMs) {
        m_timer.stop();
        m_opacity = 0.0;
        hide();
        return;
    }
    const qint64 fadeStart = kVisibleMs - kFadeMs;
    m_opacity = elapsed <= fadeStart
                    ? 1.0
                    : 1.0 - static_cast<qreal>(elapsed - fadeStart) / static_cast<qreal>(kFadeMs);
    update();
}

void VisualizerOverlay::paintEvent(QPaintEvent* /*event*/) {
    if (!showing()) {
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setOpacity(m_opacity);

    const skins::Skin* sheets = m_skin != nullptr ? &m_skin->sheets : nullptr;
    const QImage* font = sheets != nullptr ? sheets->sheet(kSheetText) : nullptr;

    const int cellW = koalaamp::skins::kCharWidth * m_scale;
    const int cellH = koalaamp::skins::kCharHeight * m_scale;
    const int cells = static_cast<int>(codepointCount(m_name));
    const int textW = font != nullptr ? cells * cellW : std::min(width(), cells * cellW * 2);
    const int x = std::max(0, (width() - textW) / 2);
    const int y = std::max(0, (height() - cellH) / kVerticalDivisor);

    // A translucent plate: a bright preset must not swallow the name.
    painter.fillRect(QRect(x - kPlatePaddingX * m_scale, y - kPlatePaddingY * m_scale,
                           textW + 2 * kPlatePaddingX * m_scale,
                           cellH + 2 * kPlatePaddingY * m_scale),
                     QColor(0, 0, 0, 170));

    if (font == nullptr) {
        // No skin font: the fallback keeps the label readable rather than blank.
        QFont fallback;
        fallback.setPixelSize(cellH + m_scale);
        painter.setFont(fallback);
        painter.setPen(QColor(255, 255, 255));
        painter.drawText(QRect(x, y, textW, cellH), Qt::AlignLeft | Qt::AlignVCenter,
                         QString::fromStdString(m_name));
        return;
    }

    int penX = x;
    for (std::size_t i = 0; i < m_name.size();) {
        const char32_t cp = nextCodepoint(m_name, i);
        if (const auto glyph = koalaamp::skins::fontGlyph(foldForFont(cp))) {
            painter.drawImage(
                QRect(penX, y, glyph->w * m_scale, glyph->h * m_scale), *font,
                QRect(glyph->x, glyph->y, glyph->w, glyph->h));
        }
        penX += cellW;  // a codepoint without a glyph leaves its cell blank
    }
}

}  // namespace koalaamp::ui
