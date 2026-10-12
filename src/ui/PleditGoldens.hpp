// SPDX-License-Identifier: MIT
//
// PleditGoldens — the per-state golden screenshot matrix of the playlist window
// (docs/tickets/SE-3-playlist-window.md acceptance): the row states (default,
// selected, current, queued, hover), multi-select, scroll, several segment
// widths and the windowshade, each rendered at 1x and 2x.
//
// `pleditGoldenColors()` deliberately uses an empty `Font=` so the renderer
// takes the 5x6 skin-font fallback: the committed SHA-256 manifest therefore
// hashes the same pixels on any machine, whatever system fonts are installed.
// The matrix itself needs no skin — it is pure layout data.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "skins/SkinText.hpp"
#include "ui/PleditState.hpp"

namespace koalaamp::ui {

/// `(name, state)` in a fixed order; `name` is also the manifest key stem.
const std::vector<std::pair<std::string, PleditState>>& pleditGoldenMatrix();

/// Looks a state up by name, or nullptr.
const PleditState* pleditGoldenState(std::string_view name);

/// Deterministic colours for the manifest: the classic defaults with no font,
/// forcing the skin-font fallback (see the file comment).
skins::PleditColors pleditGoldenColors();

}  // namespace koalaamp::ui
