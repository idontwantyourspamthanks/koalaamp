// SPDX-License-Identifier: MIT
//
// GoldenStates — the per-state golden screenshot matrix of the main window
// (docs/tickets/SE-2-main-window.md acceptance): every control in every state
// it can take, rendered at 1x and 2x.
//
// The matrix lives here (not in the test or the spike) so the offscreen
// harness, the Webamp pixel-diff and the widget tests all speak about exactly
// the same states. `frameHash` is a platform-neutral content hash (RGB triples
// in raster order) so the committed manifest in `tests/fixtures/se2/` is stable
// across byte orders and Qt builds.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include <QImage>

#include "ui/PlayerState.hpp"

namespace koalaamp::ui {

/// `(name, state)` in a fixed order; `name` is also the PNG stem.
const std::vector<std::pair<std::string, PlayerState>>& goldenStateMatrix();

/// Looks a state up by name, or nullptr.
const PlayerState* goldenState(std::string_view name);

/// Hex SHA-256 of the frame's pixels as RGB triples: same hash on any platform.
std::string frameHash(const QImage& image);

}  // namespace koalaamp::ui
