// SPDX-License-Identifier: MIT
// audio — album-keyed EQ auto-load store. See AlbumEqStore.hpp.

#include "audio/AlbumEqStore.hpp"

#include <utility>

namespace koalaamp::audio {

void AlbumEqStore::setPreset(std::string albumKey, EqPreset preset) {
    m_presets.insert_or_assign(std::move(albumKey), std::move(preset));
}

bool AlbumEqStore::removePreset(std::string_view albumKey) {
    return m_presets.erase(std::string(albumKey)) != 0;
}

std::optional<EqPreset> AlbumEqStore::preset(std::string_view albumKey) const {
    const auto it = m_presets.find(std::string(albumKey));
    if (it == m_presets.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<EqPreset> AlbumEqStore::onAlbumChanged(std::string_view albumKey) {
    if (m_enabled) {
        const auto it = m_presets.find(std::string(albumKey));
        if (it != m_presets.end()) {
            if (m_hook) {
                m_hook(true, it->second);
            }
            return it->second;
        }
    }
    if (m_hook) {
        m_hook(false, EqPreset{});
    }
    return std::nullopt;
}

} // namespace koalaamp::audio
