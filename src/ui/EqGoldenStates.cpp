// SPDX-License-Identifier: MIT
#include "ui/EqGoldenStates.hpp"

#include <string>

#include "audio/EqPreset.hpp"

namespace koalaamp::ui {

namespace {

EqWindowState baseEqState() {
    // Webamp's startup EQ: all sliders at the middle, ON off, AUTO off,
    // volume 78 / balance 0 mirrored from the main window.
    EqWindowState state;
    state.volume = 78;
    state.balance = 0;
    return state;
}

EqWindowState withSlider(EqWindowState state, int index, int value) {
    state.sliders[static_cast<std::size_t>(index)] = value;
    return state;
}

}  // namespace

const std::vector<std::pair<std::string, EqWindowState>>& eqGoldenStateMatrix() {
    static const std::vector<std::pair<std::string, EqWindowState>> states = [] {
        std::vector<std::pair<std::string, EqWindowState>> out;
        const auto add = [&out](std::string name, const EqWindowState& state) {
            out.emplace_back(std::move(name), state);
        };

        add("eq_default", baseEqState());
        {
            EqWindowState state = baseEqState();
            state.enabled = true;
            add("eq_on", state);
        }
        {
            EqWindowState state = baseEqState();
            state.enabled = true;
            state.autoLoad = true;
            add("eq_on_auto", state);
        }
        {
            EqWindowState state = baseEqState();
            state.selected = false;
            add("eq_inactive", state);
        }
        // Preamp extremes (+12 dB dark end / -12 dB) with the bands therefore
        // never flat: the ticket asks for the preamp extremes explicitly.
        add("eq_preamp_maxboost", withSlider(baseEqState(), 0, kEqValueBoost));
        add("eq_preamp_maxcut", withSlider(baseEqState(), 0, kEqValueCut));
        {
            EqWindowState state = baseEqState();
            for (int i = 1; i < kEqWindowSliders; ++i) {
                state.sliders[static_cast<std::size_t>(i)] = kEqValueBoost;
            }
            add("eq_bands_maxboost", state);
        }
        {
            EqWindowState state = baseEqState();
            for (int i = 1; i < kEqWindowSliders; ++i) {
                state.sliders[static_cast<std::size_t>(i)] = kEqValueCut;
            }
            add("eq_bands_maxcut", state);
        }
        // The "Classical" classic preset (AU-1's 17-preset set, index 0).
        {
            EqWindowState state = baseEqState();
            const auto& preset = audio::classicEqPresets()[0];
            state.enabled = true;
            state.presetIndex = 0;
            state.sliders[0] = preset.preamp;
            for (int i = 0; i < kEqWindowBands; ++i) {
                state.sliders[static_cast<std::size_t>(i + 1)] =
                    preset.bands[static_cast<std::size_t>(i)];
            }
            add("eq_preset_classical", state);
        }
        {
            EqWindowState state = baseEqState();
            state.activeSlider = static_cast<int>(EqSlider::Hz1000);
            state.sliders[static_cast<std::size_t>(EqSlider::Hz1000)] = kEqValueBoost;
            add("eq_slider_dragging", state);
        }
        {
            EqWindowState state = baseEqState();
            state.onPressed = true;
            add("eq_on_pressed", state);
        }
        {
            EqWindowState state = baseEqState();
            state.autoPressed = true;
            state.autoLoad = true;
            add("eq_auto_pressed", state);
        }
        {
            EqWindowState state = baseEqState();
            state.presetsPressed = true;
            add("eq_presets_pressed", state);
        }
        {
            EqWindowState state = baseEqState();
            state.closePressed = true;
            state.shadePressed = true;
            add("eq_title_pressed", state);
        }
        // Windowshade.
        {
            EqWindowState state = baseEqState();
            state.shade = true;
            add("eq_shade", state);
        }
        {
            EqWindowState state = baseEqState();
            state.shade = true;
            state.selected = false;
            add("eq_shade_inactive", state);
        }
        {
            EqWindowState state = baseEqState();
            state.shade = true;
            state.volume = 100;
            state.balance = -100;
            add("eq_shade_extremes", state);
        }
        {
            EqWindowState state = baseEqState();
            state.shade = true;
            state.volume = 0;
            state.balance = 0;
            add("eq_shade_volume_zero", state);
        }
        {
            EqWindowState state = baseEqState();
            state.shade = true;
            state.closePressed = true;
            state.shadePressed = true;
            add("eq_shade_pressed", state);
        }
        return out;
    }();
    return states;
}

const EqWindowState* eqGoldenState(std::string_view name) {
    for (const auto& [stateName, state] : eqGoldenStateMatrix()) {
        if (stateName == name) {
            return &state;
        }
    }
    return nullptr;
}

}  // namespace koalaamp::ui
