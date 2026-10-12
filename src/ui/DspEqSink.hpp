// SPDX-License-Identifier: MIT
//
// ui — the production `IEqSink`: the bridge from the classic EQ window's slider
// pushes onto AU-1's `DspPipeline` (SE-4 Part A).
//
// The window (SE-5) builds an `audio::EqSettings` from its slider values and
// hands it across `IEqSink`; this adapter latches it into the pipeline the app
// installed at `PlaybackCore::setPcmCallback`. It adds no engine surface: it
// only forwards settings through AU-1's public `setSettings`.
//
// ON/OFF semantics: the window's ON maps to the whole DSP-stage enable (off is
// a bit-exact bypass, docs/spec.md §3.1) and to `EqSettings::enabled`; AU-1's
// `Equalizer` additionally bypasses itself when every band/in the flat state.
#pragma once

#include "audio/DspPipeline.hpp"
#include "ui/EqSink.hpp"

namespace koalaamp::ui {

class DspEqSink final : public IEqSink {
public:
    explicit DspEqSink(audio::DspPipeline& dsp) noexcept : m_dsp(dsp) {}

    void setEqSettings(const audio::EqSettings& settings) override {
        audio::DspSettings next = m_dsp.settings();
        next.enabled = settings.enabled;  // EQ ON/OFF is the stage's on/off
        next.eq = settings;
        m_dsp.setSettings(next);
    }

    audio::EqSettings eqSettings() const override { return m_dsp.settings().eq; }

private:
    audio::DspPipeline& m_dsp;
};

}  // namespace koalaamp::ui
