// SPDX-License-Identifier: MIT
//
// EqSink — the audio-side surface the equalizer window drives.
//
// Mirrors the `ITransport` seam of the main window: `src/ui` never touches the
// DSP directly, it hands an `audio::EqSettings` across this interface. The
// production implementation forwards to AU-1's `Equalizer`/`DspPipeline`; tests
// forward to a real `Equalizer` with a PCM tap. The window builds the settings
// from its slider values but never computes gains.
#pragma once

#include "audio/Equalizer.hpp"

namespace koalaamp::ui {

class IEqSink {
public:
    virtual ~IEqSink() = default;

    /// Control-thread settings push (latched by AU-1 at the next block).
    virtual void setEqSettings(const audio::EqSettings& settings) = 0;
    /// The settings the sink currently holds.
    virtual audio::EqSettings eqSettings() const = 0;
};

}  // namespace koalaamp::ui
