// SPDX-License-Identifier: MIT
//
// VisualizerWindow — the classic-skin "general purpose" (gen.bmp) window that
// shows the MilkDrop render: VZ-1's production host for the FE-14 spike.
//
// It is a `SkinnedWindow` (frameless, title-bar drag, close box) whose content
// is the `VisualizerCanvas` filling the gen frame's client rect. The engine is
// handed in by the app, never built here: this module has no dependency on the
// rendering back end beyond the seam's interface, so third-party headers and
// libraries stay out of every UI consumer (src/interop/projectm/VisualizerEngine.hpp).
//
// Hosting: FE-14 confirmed a GL child inside a masked frameless toplevel on
// Linux/X11 (docs/spikes/projectm-qt.md §4, §10). A gen window has no
// `region.txt` section — its frame is rectangular by construction — so this
// window stays unmasked; the spike's result remains the answer for any skin
// that does shape it.
//
// Unlike the main/EQ windows a gen window ships a designed fallback (GenRenderer):
// a skin without gen.bmp still gets a usable, draggable window, and a build
// without libprojectM still gets a window that says so instead of failing.
//
// docs/spec.md §5.2-5.3, §6.1; docs/tickets/VZ-1-visualizer.md task 1.
#pragma once

#include <cstdint>
#include <string>

#include <QImage>
#include <QPoint>

#include "interop/projectm/VisualizerEngine.hpp"
#include "skins/SkinSet.hpp"
#include "ui/GenRenderer.hpp"
#include "ui/SkinnedWindow.hpp"
#include "ui/VisualizerOverlay.hpp"
#include "ui/VisualizerCanvas.hpp"

namespace koalaamp::ui {

class VisualizerWindow : public SkinnedWindow {
    Q_OBJECT

public:
    /// `engine` must outlive the window (the app owns it, as it owns the skin).
    explicit VisualizerWindow(interop::visualizer::IVisualizerEngine& engine,
                              QWidget* parent = nullptr);
    ~VisualizerWindow() override;

    // --- skin ---------------------------------------------------------------
    /// The loaded skin; nullptr paints the designed fallback frame instead of
    /// the empty black window the other windows show before a skin is chosen.
    void setSkin(const skins::SkinSet* skin);
    const skins::SkinSet* skin() const { return m_skin; }
    /// True when the current skin ships no `gen.bmp` and the fallback frame is
    /// what the user sees.
    bool usesFallbackChrome() const;

    // --- engine -------------------------------------------------------------
    /// The canvas the app feeds PCM through (`canvas().pcmRing()`), and the
    /// engine it drives.
    VisualizerCanvas& canvas() { return *m_canvas; }
    const VisualizerCanvas& canvas() const { return *m_canvas; }
    interop::visualizer::IVisualizerEngine& engine() { return m_engine; }
    /// The preset-switch overlay, raised above the canvas (task 4).
    VisualizerOverlay& overlay() { return *m_overlay; }
    const VisualizerOverlay& overlay() const { return *m_overlay; }
    /// False in a build without libprojectM: the window then paints the
    /// unavailable message and hides the canvas.
    bool visualizerAvailable() const;
    /// Frames the canvas has rendered (the soak's liveness signal).
    std::uint64_t framesRendered() const;

    // --- view ---------------------------------------------------------------
    int scale() const { return m_scale; }
    void setScale(int scale) override;
    bool isDoubleSize() const { return m_scale > 1; }
    void setDoubleSize(bool on);
    void setAlwaysOnTop(bool on);
    bool isAlwaysOnTop() const;

    /// The window title, drawn in the skin's 5x6 font (uppercase-folded) and set
    /// as the WM caption. Winamp gen windows show the plugin's name.
    void setTitle(const std::string& title);
    const std::string& title() const { return m_title; }

    /// Content size in 1x skin pixels, snapped to the gen tiling grid.
    QSize contentSize() const { return genSnapSize(m_clientSize); }
    /// Request a 1x client size; snapped with `genSnapSize` (never smaller than
    /// the 194x109 reference window).
    void setClientSize(const QSize& clientSize);

    /// The client rectangle (1x skin pixels) the canvas fills — the part of the
    /// frame a plugin paints into.
    QRect clientRect() const { return genLayout(contentSize()).client; }
    /// The close box, in 1x skin pixels (the window scales it).
    QRect closeButtonRect() const { return genLayout(contentSize()).closeButton; }

    // --- fullscreen (task 6) --------------------------------------------------
    /// Borderless fullscreen: the render alone, no gen chrome. ESC (or F) leaves
    /// it and restores the windowed geometry and shape.
    void setFullscreen(bool on);
    bool isFullscreen() const { return m_fullscreen; }

    // --- preset announcements (task 4) ---------------------------------------
    /// Announces a preset switch: the overlay shows the file's display name and
    /// fades it out. Empty clears it.
    void showPresetName(const std::string& path);
    /// The name currently being announced ("" when the overlay is clear).
    std::string presetName() const { return m_presetName; }
    bool isPresetNameVisible() const;
    qreal presetNameOpacity() const;

signals:
    void closeRequested();
    /// MilkDrop's keys, forwarded as requests: the app owns the cycler, this window
    /// only knows what the user asked for (N/P move, R toggles random, L locks).
    void nextPresetRequested();
    void previousPresetRequested();
    void orderToggleRequested();
    void lockedToggleRequested();
    /// F (and ESC, when leaving) report the state so the app can react to it.
    void fullscreenChanged(bool on);
    void doubleSizeChanged(bool on);
    void alwaysOnTopChanged(bool on);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    /// Keeps the canvas and overlay in step with the window: `setFullscreen()` can
    /// only lay out for the size it sees, and the window manager resizes later.
    void resizeEvent(QResizeEvent* event) override;
    bool isTitleBarAt(const QPoint& point) const override;

private:
    void updateShape();
    void layoutCanvas();
    /// A 1x skin-pixel rectangle in device pixels (the window's scale applied).
    QRect toDevice(const QRect& rect) const;
    void applyTitle();
    void drawUnavailableMessage(QPainter& painter);

    interop::visualizer::IVisualizerEngine& m_engine;
    VisualizerCanvas* m_canvas = nullptr;
    VisualizerOverlay* m_overlay = nullptr;
    std::string m_presetName;
    bool m_fullscreen = false;
    QRect m_windowedGeometry;
    const skins::SkinSet* m_skin = nullptr;
    QSize m_clientSize{kGenWidth, kGenHeight};
    std::string m_title = "VISUALIZER";
    int m_scale = 1;
    bool m_closePressed = false;
};

}  // namespace koalaamp::ui
