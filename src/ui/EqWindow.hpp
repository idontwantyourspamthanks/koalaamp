// SPDX-License-Identifier: MIT
//
// The classic Winamp equalizer window: 275x116 (or a 275x14 windowshade),
// preamp + 10 band sliders, ON/AUTO/PRESETS, the dB reset zones and the EQ
// graph — bound live to AU-1's `Equalizer` through the `IEqSink` seam.
//
// Geometry, sprite selection and behaviour are ported from Webamp (MIT)
// `css/equalizer-window.css` / `js/components/EqualizerWindow/*` at
// webamp@e6283c0eabe9b1313cd41df16d2875e897719696; the FE-3 spike's corrected
// `eq_ex` metrics (275x82) apply. Attribution in LICENSES.md.
//
// The window *renders and routes*: slider values are AU-1's 0-63 field and are
// handed to `audio::EqSettings` verbatim. It never computes gains, and it never
// applies a preset by itself — preset/auto-load state lives in AU-1
// (`EqPreset`, `AlbumEqStore`).
//
// Standalone-movable (the SE-2 pattern). SE-4's window manager will own
// snapping; this window emits `dragFinished()` from `SkinnedWindow` for that
// seam and does not implement any snapping itself.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <QPoint>

#include "audio/AlbumEqStore.hpp"
#include "audio/EqPreset.hpp"
#include "skins/SkinSet.hpp"
#include "ui/EqHitRegion.hpp"
#include "ui/EqSink.hpp"
#include "ui/EqState.hpp"
#include "ui/SkinnedWindow.hpp"

namespace koalaamp::ui {

class EqWindow : public SkinnedWindow {
    Q_OBJECT

public:
    static constexpr int kWidth = 275;
    static constexpr int kHeight = 116;
    static constexpr int kShadeHeight = 14;

    explicit EqWindow(QWidget* parent = nullptr);
    ~EqWindow() override;

    // --- skin ---------------------------------------------------------------
    /// The loaded skin; nullptr renders the empty (black) window.
    void setSkin(const skins::SkinSet* skin);
    const skins::SkinSet* skin() const { return m_skin; }

    // --- audio seam ---------------------------------------------------------
    void setEqSink(IEqSink* sink);
    IEqSink* eqSink() const { return m_sink; }
    /// AU-1's album-keyed auto-load store (non-owning, optional). AUTO toggles
    /// its `enabled` flag; `notifyAlbumChanged` drives the indicator hook.
    void setAlbumEqStore(audio::AlbumEqStore* store);
    audio::AlbumEqStore* albumEqStore() const { return m_albumStore; }
    /// Called when playback moves to a new album: consults the store and applies
    /// the preset when auto-load is on. Returns true when a preset was applied.
    bool notifyAlbumChanged(const std::string& albumKey);

    // --- render state -------------------------------------------------------
    const EqWindowState& state() const { return m_state; }
    /// Replaces the render state wholesale (offscreen harness / transport sync)
    /// and pushes the resulting EqSettings if they changed.
    void setState(const EqWindowState& state);

    // --- model --------------------------------------------------------------
    int sliderValue(int index) const;
    /// Sets one slider (AU-1 0..63) and pushes the settings. Marks the preset
    /// custom unless the change matches it exactly.
    void setSliderValue(int index, int auValue);
    /// Every slider (preamp included) to unity — the ticket's RESET.
    void resetAll();
    /// The 10 bands (not the preamp) to `auValue` — Webamp's +/-12/0 dB zones.
    void setAllBands(int auValue);
    /// Snapshots the sliders into AU-1 settings.
    audio::EqSettings settingsFromState() const;
    /// Applies settings to the sliders (control-thread mirror of the DSP).
    void applyEqSettings(const audio::EqSettings& settings, int presetIndex = -1);
    void applyPreset(const audio::EqPreset& preset, int presetIndex = -1);
    /// Loads one of AU-1's 17 classic presets; false when out of range.
    bool loadClassicPreset(int index);
    const std::vector<audio::EqPreset>& userPresets() const { return m_userPresets; }
    const std::vector<audio::EqPreset>& classicPresets() const;
    /// Parses a `.eqf`/`winamp.q1` byte range (AU-1's codec) and appends the
    /// presets. Returns the number imported.
    int importPresetsFromBytes(const std::vector<std::uint8_t>& bytes);
    /// Writes the current sliders as a `.eqf` (AU-1's byte-compatible codec).
    bool savePresetToFile(const std::string& path) const;
    /// Loads a single `.eqf` and applies it; false on a parse failure.
    bool loadPresetFromFile(const std::string& path);
    /// Imports a `.eqf`/`winamp.q1` file into the user preset list.
    bool importPresetsFromFile(const std::string& path);
    int presetIndex() const { return m_state.presetIndex; }

    // --- view ---------------------------------------------------------------
    int scale() const { return m_scale; }
    void setScale(int scale) override;
    bool isDoubleSize() const { return m_scale > 1; }
    void setDoubleSize(bool on);
    bool isShaded() const { return m_state.shade; }
    void setShade(bool shaded);
    void setAlwaysOnTop(bool on);
    bool isAlwaysOnTop() const;
    void setVolumeBalance(int volume, int balance);

    /// When false the PRESETS button does not open a dialog/menu (tests,
    /// headless runs); the direct preset API stays available.
    void setPresetDialogsEnabled(bool on) { m_presetDialogsEnabled = on; }
    bool presetDialogsEnabled() const { return m_presetDialogsEnabled; }

    /// The interaction-parity checklist this window implements; each id has a
    /// scripted-input test in tests/se5_interaction_test.cpp.
    static const std::vector<std::string>& interactionChecklist();

signals:
    void eqEnabledChanged(bool on);
    void autoLoadChanged(bool on);
    void presetSelected(int index);
    void shadeChanged(bool shaded);
    void closeRequested();
    void doubleSizeChanged(bool on);
    void alwaysOnTopChanged(bool on);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool isTitleBarAt(const QPoint& point) const override;

private:
    void updateShape();
    void requestRepaint();
    void pushSettings();
    void setSliderFromPoint(int index, const QPoint& point, bool preserveGrab);
    void activate(EqHitId id, int sliderIndex);
    void showPresetsMenu();
    void openPresetDialog();
    void savePresetDialog();

    const skins::SkinSet* m_skin = nullptr;
    IEqSink* m_sink = nullptr;
    audio::AlbumEqStore* m_albumStore = nullptr;
    EqWindowState m_state;
    audio::EqMode m_mode = audio::EqMode::WinampCurve;
    std::vector<audio::EqPreset> m_userPresets;
    int m_scale = 1;
    bool m_presetDialogsEnabled = true;

    EqHitId m_dragging = EqHitId::None;
    int m_dragSlider = -1;
    int m_grabOffset = 0;   ///< pointer offset inside the drag target
    int m_focusSlider = 0;  ///< keyboard target
};

}  // namespace koalaamp::ui
