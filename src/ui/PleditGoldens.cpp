// SPDX-License-Identifier: MIT
#include "ui/PleditGoldens.hpp"

#include <algorithm>

namespace koalaamp::ui {

namespace {

PleditEntry entry(int number, int lengthMs) {
    PleditEntry e;
    e.title = "Artist - Track " + std::to_string(number);
    e.filename = "/music/track-" + std::to_string(number) + ".mp3";
    e.lengthMs = lengthMs;
    return e;
}

/// A fixed 24-entry list: enough to scroll at the default 4-row height.
std::vector<PleditEntry> sampleEntries() {
    std::vector<PleditEntry> entries;
    entries.reserve(24);
    for (int i = 1; i <= 24; ++i) {
        entries.push_back(entry(i, 185'000 + i * 1000));
    }
    return entries;
}

PleditState baseState() {
    PleditState state;
    state.entries = sampleEntries();
    return state;
}

}  // namespace

const std::vector<std::pair<std::string, PleditState>>& pleditGoldenMatrix() {
    static const std::vector<std::pair<std::string, PleditState>> matrix = [] {
        std::vector<std::pair<std::string, PleditState>> out;
        const auto add = [&out](std::string name, const PleditState& state) {
            out.emplace_back(std::move(name), state);
        };

        add("default", baseState());

        {
            PleditState state = baseState();
            selectSingle(state, 1);
            add("selected", state);
        }
        {
            PleditState state = baseState();
            state.currentIndex = 2;
            add("current", state);
        }
        {
            PleditState state = baseState();
            enqueueIndex(state, 3);
            add("queued", state);
        }
        {
            PleditState state = baseState();
            enqueueIndex(state, 3);
            enqueueIndex(state, 1);
            state.currentIndex = 5;
            add("queued-multi", state);
        }
        {
            PleditState state = baseState();
            selectRange(state, 1, 3);
            add("multi-select", state);
        }
        {
            PleditState state = baseState();
            state.hoverIndex = 2;
            add("hover", state);
        }
        {
            PleditState state = baseState();
            state.scrollOffset = 7;
            add("scrolled", state);
        }
        {
            PleditState state = baseState();
            state.widthSegments = 1;
            add("width-1", state);
        }
        {
            PleditState state = baseState();
            state.widthSegments = 3;
            add("width-3", state);
        }
        {
            PleditState state = baseState();
            state.heightSegments = 2;
            add("tall", state);
        }
        {
            PleditState state = baseState();
            state.widthSegments = 2;
            state.heightSegments = 1;
            state.scrollOffset = 4;
            selectRange(state, 4, 6);
            state.currentIndex = 5;
            enqueueIndex(state, 6);
            add("wide-tall-mixed", state);
        }
        {
            PleditState state = baseState();
            state.focused = false;
            add("inactive", state);
        }
        {
            PleditState state = baseState();
            state.shade = true;
            add("shade", state);
        }
        {
            PleditState state = baseState();
            state.shade = true;
            state.focused = false;
            add("shade-inactive", state);
        }
        {
            PleditState state = baseState();
            state.entries.clear();
            add("empty", state);
        }
        return out;
    }();
    return matrix;
}

const PleditState* pleditGoldenState(std::string_view name) {
    for (const auto& [stateName, state] : pleditGoldenMatrix()) {
        if (stateName == name) {
            return &state;
        }
    }
    return nullptr;
}

skins::PleditColors pleditGoldenColors() {
    skins::PleditColors colors = skins::defaultPleditColors();
    colors.font.clear();
    return colors;
}

}  // namespace koalaamp::ui
