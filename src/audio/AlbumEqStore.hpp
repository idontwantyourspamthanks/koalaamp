// SPDX-License-Identifier: MIT
// audio — album-keyed EQ auto-load store (AU-1 task: "Auto-load EQ per track
// (album-keyed store) + indicator hook").
//
// Winamp's "auto" presets keyed on the filename (winamp.q2); KoalaAmp keys the
// store on the album identity so a whole album gets one preset. This is a
// control-thread helper: the UI (SE-5) sets presets/auto-load and observes the
// indicator hook; the DSP application goes through EqSettings.

#pragma once

#include "audio/EqPreset.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace koalaamp::audio {

class AlbumEqStore {
public:
    /// Indicator hook: `active` is true when an album preset was found and
    /// auto-load applied it (the EQ window lights its "auto" indicator).
    using IndicatorHook = std::function<void(bool active, const EqPreset& preset)>;

    void setEnabled(bool enabled) noexcept { m_enabled = enabled; }
    bool enabled() const noexcept { return m_enabled; }

    void setPreset(std::string albumKey, EqPreset preset);
    bool removePreset(std::string_view albumKey);
    std::optional<EqPreset> preset(std::string_view albumKey) const;
    std::size_t size() const noexcept { return m_presets.size(); }
    void clear() { m_presets.clear(); }

    void setIndicatorHook(IndicatorHook hook) { m_hook = std::move(hook); }

    /// Called when playback moves to a new album. When auto-load is enabled and
    /// a preset exists, returns it (and fires the indicator hook with
    /// active=true); otherwise returns nullopt (hook fires active=false with a
    /// neutral preset).
    std::optional<EqPreset> onAlbumChanged(std::string_view albumKey);

private:
    bool m_enabled{false};
    std::unordered_map<std::string, EqPreset> m_presets;
    IndicatorHook m_hook;
};

} // namespace koalaamp::audio
