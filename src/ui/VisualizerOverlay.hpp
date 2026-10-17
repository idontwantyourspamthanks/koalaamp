// SPDX-License-Identifier: MIT
//
// VisualizerOverlay — the preset-switch overlay (VZ-1 task 4): the preset's name
// over the render, fading out, the way Winamp's visualizer windows announce a new
// preset.
//
// It is a *child widget* raised above the GL canvas rather than something painted
// into it: the canvas hands projectM its FBO and nothing else, so painting there
// would mean interleaving Qt painting with projectM's raw GL state every frame. A
// translucent sibling leaves the GL path untouched.
//
// The name is drawn in the skin's 5x6 `text.bmp` font when the skin has one — the
// same glyphs GenRenderer uses for the title bar — on a translucent plate so it
// stays readable over any preset.
#pragma once

#include <string>

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

namespace koalaamp::skins {
class SkinSet;
}

namespace koalaamp::ui {

class VisualizerOverlay : public QWidget {
    Q_OBJECT

public:
    /// How long the name stays up, and how much of that is the fade out.
    static constexpr int kVisibleMs = 2000;
    static constexpr int kFadeMs = 750;
    /// Fade granularity (~60 fps while the name is up, and never otherwise).
    static constexpr int kTickMs = 16;

    explicit VisualizerOverlay(QWidget* parent = nullptr);

    /// The skin whose font the name is drawn in; nullptr paints the fallback font.
    void setSkin(const skins::SkinSet* skin);
    /// Integer skin scale, so the overlay matches the window it covers.
    void setScale(int scale);

    /// Announces `name` (a display name, not a path). Empty clears it now.
    void showName(const std::string& name);

    bool showing() const { return m_opacity > 0.0 && !m_name.empty(); }
    qreal opacity() const { return m_opacity; }

protected:
    void paintEvent(QPaintEvent* event) override;
    /// The fade clock starts when the overlay is really on screen: a preset
    /// announced before the window is mapped would otherwise burn its 2 s unseen.
    void showEvent(QShowEvent* event) override;

private:
    void tick();

    const skins::SkinSet* m_skin = nullptr;
    std::string m_name;
    int m_scale = 1;
    qreal m_opacity = 0.0;
    QElapsedTimer m_since;
    QTimer m_timer;
};

}  // namespace koalaamp::ui
