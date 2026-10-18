// SPDX-License-Identifier: MIT
//
// The classic Winamp main (player) window: 275x116 (or a 275x14 windowshade
// bar), every control of the classic surface, driven by `PlaybackCore` through
// the `ITransport` seam.
//
// Geometry, sprite selection and behaviour are ported from Webamp (MIT)
// `css/main-window.css` / `js/skinSelectors.ts` / `js/components/*` at
// webamp@e6283c0eabe9b1313cd41df16d2875e897719696; the FE-3 spike proved the
// core of the composition pixel-identical to Webamp and SE-2 keeps that
// harness green for the extended state matrix (attribution in LICENSES.md).
//
// Layout: `MainWindow` (frameless toplevel, `region.txt` shape) -> `SkinCanvas`
// child, transparent to mouse events so the window owns all input.
#pragma once

#include <memory>
#include <string>

#include <QPoint>
#include <QTimer>

#include "skins/SkinSet.hpp"
#include "ui/PlayerState.hpp"
#include "ui/SkinHitRegion.hpp"
#include "ui/SkinCanvas.hpp"
#include "ui/SkinnedWindow.hpp"
#include "ui/Transport.hpp"
#include "ui/VisFeed.hpp"

namespace koalaamp::ui {

/// Winamp's marquee step: one cell every 220 ms (Webamp's marquee interval).
inline constexpr int kMarqueeStepMs = 220;
/// Animation tick: ~60 fps.
inline constexpr int kTickMs = 16;
/// Width of the balance slider's centre detent, in slider *pixels* on either
/// side of the middle: inside it the pan is exactly 0, so the mono centre is a
/// detent rather than a knife edge (the thumb travel is only 24 px for the full
/// +/-100 range, so a value-domain threshold would never trigger in practice).
inline constexpr int kBalanceCentreDetent = 2;

class MainWindow : public SkinnedWindow {
    Q_OBJECT

public:
    static constexpr int kWidth = 275;
    static constexpr int kHeight = 116;
    static constexpr int kShadeHeight = 14;

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // --- skin ---------------------------------------------------------------
    /// The loaded skin. `nullptr` renders an empty (black) window, which is
    /// what the app shows before a skin is chosen.
    void setSkin(const skins::SkinSet* skin);
    const skins::SkinSet* skin() const { return m_skin; }

    // --- playback -----------------------------------------------------------
    void setTransport(ITransport* transport);
    ITransport* transport() const { return m_transport; }
    /// The PCM tap the engine writes into (see VisFeed).
    VisFeed& visFeed() { return m_visFeed; }
    /// Pulls the transport's state into the render state and repaints.
    void syncFromTransport();

    // --- render state -------------------------------------------------------
    const PlayerState& state() const { return m_state; }
    /// Replaces the render state wholesale (used by the offscreen harnesses).
    void setState(const PlayerState& state);

    // --- view ---------------------------------------------------------------
    int scale() const { return m_scale; }
    void setScale(int scale) override;
    bool isDoubleSize() const { return m_scale > 1; }
    void setDoubleSize(bool on);
    bool isShaded() const { return m_state.shade; }
    void setShade(bool shaded);
    void setAlwaysOnTop(bool on);
    bool isAlwaysOnTop() const;
    SkinCanvas& canvas() { return *m_canvas; }
    const SkinCanvas& canvas() const { return *m_canvas; }

    /// True when no transport is attached (or the feed is starved): the
    /// visualiser then animates from `syntheticVisFrame` so the window still
    /// shows a live spectrum offscreen.
    void setSyntheticVisual(bool on);
    bool usesSyntheticVisual() const { return m_syntheticVisual; }

    /// When false, eject emits `ejectRequested()` without opening a dialog
    /// (tests, headless runs).
    void setFileDialogEnabled(bool on) { m_fileDialogEnabled = on; }
    bool fileDialogEnabled() const { return m_fileDialogEnabled; }

    /// Marquee animation step (one cell per tick of 220 ms).
    int marqueeStep() const { return m_marqueeStep; }
    /// Elapsed animation time in ms since the window was created.
    qint64 animationMs() const { return m_animationMs; }

    /// Advances the animation one tick. Called by the internal timer; exposed
    /// so tests can step deterministically instead of sleeping.
    void tick();

    /// The full interaction-parity checklist this window implements
    /// (docs/tickets/SE-2-main-window.md); each id has a scripted-input test.
    static const std::vector<std::string>& interactionChecklist();

signals:
    void eqToggled(bool selected);
    void playlistToggled(bool selected);
    void visualizerToggled(bool selected);
    void fileInfoRequested();
    void menuRequested();
    void ejectRequested();
    void closeRequested();
    void alwaysOnTopChanged(bool on);
    void doubleSizeChanged(bool on);
    void shadeChanged(bool shaded);
    void mediaOpenRequested();  ///< the play button with nothing loaded

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool isTitleBarAt(const QPoint& point) const override;

private:
    void buildUi();
    void applySkinToCanvas();
    void updateShape();
    void setRenderState(const PlayerState& state);
    void requestRepaint();
    void updateTickTimer();

    // Input
    HitId controlAt(const QPoint& point) const;
    void pressControl(HitId id, const QPoint& point);
    void dragControl(HitId id, const QPoint& point);
    void releaseControl(HitId id, const QPoint& point);
    void activate(HitId id);

    // Model helpers
    void toggleShuffle();
    void toggleRepeat();
    void toggleTimeMode();
    void cycleVisualizer();
    void seekRelative(int seconds);
    void seekPercentFromPoint(int localX, bool shadePosition);
    void setVolumeFromPoint(int localX);
    void setBalanceFromPoint(int localX);
    void openFiles();
    void playCurrent();

    const skins::SkinSet* m_skin = nullptr;
    ITransport* m_transport = nullptr;
    VisFeed m_visFeed;
    SkinCanvas* m_canvas = nullptr;
    QTimer m_timer;
    PlayerState m_state;
    int m_scale = 1;
    bool m_syntheticVisual = true;
    bool m_fileDialogEnabled = true;

    HitId m_pressed = HitId::None;   ///< button currently held down
    HitId m_dragging = HitId::None;  ///< slider currently dragged

    int m_marqueeStep = 0;
    qint64 m_animationMs = 0;
    qint64 m_pauseMs = 0;  ///< time in the Paused state (blink phase)
    int m_visStep = 0;
    std::string m_mediaTitle;  ///< last non-empty title, kept while stopped
    std::unique_ptr<playback::MediaRef> m_currentMedia;
};

/// The complete interaction-parity list (see interactionChecklist()).
const std::vector<std::string>& interactionParityChecklist();

}  // namespace koalaamp::ui
