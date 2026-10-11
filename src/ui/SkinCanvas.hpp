// SPDX-License-Identifier: MIT
//
// SkinCanvas — the painting surface of a skinned window (docs/spec.md §5.2).
//
// It composes from pre-decoded sprite slices with exact integer blits, at
// integer scale only, and caches the "chrome" (everything except the live
// visualiser columns) so a 60 fps spectrum repaint costs one cached blit plus
// ~~1200 one-pixel fills instead of a full window composition.
#pragma once

#include <cstdint>

#include <QImage>
#include <QWidget>

#include "skins/Skin.hpp"
#include "skins/SkinText.hpp"
#include "ui/PlayerState.hpp"

namespace koalaamp::ui {

class SkinCanvas : public QWidget {
    Q_OBJECT

public:
    explicit SkinCanvas(QWidget* parent = nullptr);

    /// The decoded sheets and the skin's viscolor table. A null skin paints
    /// the empty window (black) — the app runs before a skin is chosen.
    void setSkin(const skins::Skin* sheets, const skins::VisColor& visColor);

    /// Sets the state to paint. Repaints only what actually changed: the chrome
    /// is re-composed when any non-visualiser field moves, the visualiser
    /// columns are redrawn every time.
    void setState(const PlayerState& state);
    const PlayerState& state() const { return m_state; }

    /// Integer scale (1, 2, 3...). Sprite blits are never fractional
    /// (docs/spec.md §5.2).
    void setScale(int scale);
    int scale() const { return m_scale; }

    /// Window content size in device pixels (275 x 116, or x14 shaded).
    QSize contentSize() const;
    QSize sizeHint() const override { return contentSize(); }
    QSize minimumSizeHint() const override { return contentSize(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void ensureChrome();
    void invalidateChrome();

    const skins::Skin* m_sheets = nullptr;
    skins::VisColor m_vis;
    PlayerState m_state;
    int m_scale = 1;
    QImage m_chrome;
    std::uint64_t m_chromeKey = 0;
    bool m_chromeValid = false;
};

}  // namespace koalaamp::ui
