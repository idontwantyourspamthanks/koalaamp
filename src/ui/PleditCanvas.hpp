// SPDX-License-Identifier: MIT
//
// PleditCanvas — the painting surface of the playlist window, mirroring
// `SkinCanvas` (SE-2): the window owns every pointer event, the canvas is a
// pure paint surface, and compositions come from a pure renderer at integer
// scale only. The playlist has no per-frame overlay, so the whole 1x frame is
// cached and re-composed only when the state changes.
#pragma once

#include <QImage>
#include <QWidget>

#include "skins/Skin.hpp"
#include "skins/SkinText.hpp"
#include "ui/PleditState.hpp"

namespace koalaamp::ui {

class PleditCanvas : public QWidget {
    Q_OBJECT

public:
    explicit PleditCanvas(QWidget* parent = nullptr);

    /// Decoded `pledit.bmp` sheet + the skin's pledit colours. A null skin
    /// paints the empty (background-coloured) window.
    void setSkin(const skins::Skin* sheets, const skins::PleditColors& colors);
    const skins::Skin* skin() const { return m_sheets; }

    void setState(const PleditState& state);
    const PleditState& state() const { return m_state; }

    void setScale(int scale);
    int scale() const { return m_scale; }

    QSize contentSize() const;
    QSize sizeHint() const override { return contentSize(); }
    QSize minimumSizeHint() const override { return contentSize(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void recompose();

    const skins::Skin* m_sheets = nullptr;
    skins::PleditColors m_colors;
    PleditState m_state;
    int m_scale = 1;
    QImage m_frame;
};

}  // namespace koalaamp::ui
