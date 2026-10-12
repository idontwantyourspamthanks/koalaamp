// SPDX-License-Identifier: MIT
//
// EqGoldenStates — the per-state golden screenshot matrix of the equalizer
// window (docs/tickets/SE-5-eq-window.md acceptance): the window in every state
// it can take, rendered at 1x and 2x.
//
// Kept out of the test so the offscreen harness and the widget tests speak
// about exactly the same states. `frameHash` (GoldenStates.hpp) is the shared
// platform-neutral digest.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "ui/EqState.hpp"

namespace koalaamp::ui {

/// `(name, state)` in a fixed order; `name` is also the PNG stem.
const std::vector<std::pair<std::string, EqWindowState>>& eqGoldenStateMatrix();

/// Looks a state up by name, or nullptr.
const EqWindowState* eqGoldenState(std::string_view name);

}  // namespace koalaamp::ui
