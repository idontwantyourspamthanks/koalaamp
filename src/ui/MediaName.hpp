// SPDX-License-Identifier: MIT
//
// MediaName — Winamp's title fallback for a medium with no readable tags: the
// file name without its extension (paths) or the last path segment (URLs).
#pragma once

#include <string>

#include "playback/IAudioEngine.hpp"

namespace koalaamp::ui {

/// Display name for `media` as it appears in the song-title marquee.
std::string mediaDisplayName(const playback::MediaRef& media);

}  // namespace koalaamp::ui
