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
// ON/OFF semantics: the window's ON maps ONLY to `EqSettings::enabled` — the
// EQ section's own gate (AU-1's `Equalizer` bypasses itself when off; a
// bit-exact section bypass). It must NOT touch `DspSettings::enabled`, the
// whole-stage gate that also covers ReplayGain and the limiter — that flag
// belongs to app startup (main.cpp) and stays true while RG/limiter are
// wanted. (This header previously documented the coupling; it caused the
// EQ-off-kills-ReplayGain bug.)
#pragma once

#include "audio/DspPipeline.hpp"
#include "ui/EqSink.hpp"

namespace koalaamp::ui {

class DspEqSink final : public IEqSink {
public:
    explicit DspEqSink(audio::DspPipeline& dsp) noexcept : m_dsp(dsp) {}

    void setEqSettings(const audio::EqSettings& settings) override {
        // EQ ON/OFF toggles the EQ section only (settings.eq.enabled) — it
        // must NOT touch DspSettings::enabled, which gates the whole DSP
        // stage (ReplayGain + limiter included). The whole-stage flag belongs
        // to app startup (true while RG/limiter are wanted) and stays true
        // here; EQ-off then flattens the bands while RG still applies.
        audio::DspSettings next = m_dsp.settings();
        next.eq = settings;
        m_dsp.setSettings(next);
    }

    audio::EqSettings eqSettings() const override { return m_dsp.settings().eq; }

private:
    audio::DspPipeline& m_dsp;
};

}  // namespace koalaamp::ui
