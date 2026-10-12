// SPDX-License-Identifier: MIT
#include "ui/PleditState.hpp"

#include <algorithm>
#include <utility>

namespace koalaamp::ui {

namespace {

/// Remap table from old row indices to new ones; -1 marks a removed row.
std::vector<int> identityMap(std::size_t size) {
    std::vector<int> map(size);
    for (std::size_t i = 0; i < size; ++i) {
        map[i] = static_cast<int>(i);
    }
    return map;
}

}  // namespace

bool isSelected(const PleditState& state, std::size_t index) {
    return std::binary_search(state.selected.begin(), state.selected.end(), index);
}

std::size_t queuePosition(const PleditState& state, std::size_t index) {
    const auto it = std::find(state.queue.begin(), state.queue.end(), index);
    if (it == state.queue.end()) {
        return 0;
    }
    return static_cast<std::size_t>(std::distance(state.queue.begin(), it)) + 1;
}

std::size_t selectedCount(const PleditState& state) { return state.selected.size(); }

void selectSingle(PleditState& state, std::size_t index) {
    state.selected.assign(1, index);
}

void selectToggle(PleditState& state, std::size_t index) {
    const auto it = std::lower_bound(state.selected.begin(), state.selected.end(), index);
    if (it != state.selected.end() && *it == index) {
        state.selected.erase(it);
    } else {
        state.selected.insert(it, index);
    }
}

void selectRange(PleditState& state, std::size_t anchor, std::size_t index) {
    const std::size_t lo = std::min(anchor, index);
    const std::size_t hi = std::max(anchor, index);
    state.selected.clear();
    for (std::size_t i = lo; i <= hi; ++i) {
        state.selected.push_back(i);
    }
}

void clearSelection(PleditState& state) { state.selected.clear(); }

void selectAll(PleditState& state) {
    state.selected.clear();
    state.selected.reserve(state.entries.size());
    for (std::size_t i = 0; i < state.entries.size(); ++i) {
        state.selected.push_back(i);
    }
}

std::size_t removeSelected(PleditState& state) {
    if (state.selected.empty()) {
        return 0;
    }
    std::vector<int> map = identityMap(state.entries.size());
    std::vector<PleditEntry> kept;
    kept.reserve(state.entries.size() - state.selected.size());
    int next = 0;
    std::size_t removed = 0;
    auto sel = state.selected.begin();
    for (std::size_t i = 0; i < state.entries.size(); ++i) {
        if (sel != state.selected.end() && *sel == i) {
            map[i] = -1;
            ++sel;
            ++removed;
            continue;
        }
        kept.push_back(std::move(state.entries[i]));
        map[i] = next++;
    }
    state.entries = std::move(kept);

    // Queue: drop removed rows, remap the rest.
    std::vector<std::size_t> queue;
    for (const std::size_t index : state.queue) {
        if (map[index] >= 0) {
            queue.push_back(static_cast<std::size_t>(map[index]));
        }
    }
    state.queue = std::move(queue);
    if (state.currentIndex >= 0) {
        state.currentIndex =
            map[static_cast<std::size_t>(state.currentIndex)] >= 0
                ? map[static_cast<std::size_t>(state.currentIndex)]
                : -1;
    }
    clearSelection(state);
    clampScroll(state);
    return removed;
}

void moveSelected(PleditState& state, std::size_t targetIndex) {
    if (state.selected.empty()) {
        return;
    }
    const std::size_t count = state.entries.size();
    targetIndex = std::min(targetIndex, count);

    std::vector<PleditEntry> moved;
    moved.reserve(state.selected.size());
    for (const std::size_t index : state.selected) {
        moved.push_back(state.entries[index]);
    }

    // Number of unselected rows strictly before the drop target.
    std::size_t insertAt = 0;
    for (std::size_t i = 0; i < targetIndex; ++i) {
        if (!isSelected(state, i)) {
            ++insertAt;
        }
    }

    std::vector<int> map = identityMap(count);
    std::vector<PleditEntry> rebuilt;
    rebuilt.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (isSelected(state, i)) {
            continue;
        }
        const std::size_t newIndex = rebuilt.size();
        rebuilt.push_back(std::move(state.entries[i]));
        map[i] = static_cast<int>(newIndex < insertAt ? newIndex
                                                      : newIndex + moved.size());
    }
    rebuilt.insert(rebuilt.begin() + static_cast<std::ptrdiff_t>(insertAt),
                   std::make_move_iterator(moved.begin()),
                   std::make_move_iterator(moved.end()));
    // The moved block occupies [insertAt, insertAt + moved.size()).
    for (std::size_t k = 0; k < state.selected.size(); ++k) {
        map[state.selected[k]] = static_cast<int>(insertAt + k);
    }

    state.entries = std::move(rebuilt);
    std::vector<std::size_t> selection;
    selection.reserve(state.selected.size());
    for (std::size_t k = 0; k < state.selected.size(); ++k) {
        selection.push_back(insertAt + k);
    }
    state.selected = std::move(selection);

    std::vector<std::size_t> queue;
    queue.reserve(state.queue.size());
    for (const std::size_t index : state.queue) {
        if (map[index] >= 0) {
            queue.push_back(static_cast<std::size_t>(map[index]));
        }
    }
    state.queue = std::move(queue);
    if (state.currentIndex >= 0) {
        state.currentIndex = map[static_cast<std::size_t>(state.currentIndex)];
    }
    clampScroll(state);
}

void appendEntries(PleditState& state, std::vector<PleditEntry> entries) {
    state.entries.insert(state.entries.end(), std::make_move_iterator(entries.begin()),
                         std::make_move_iterator(entries.end()));
}

void clampScroll(PleditState& state) {
    const int visible = pleditVisibleTracks(state.heightSegments);
    const int maxFirst =
        std::max(0, static_cast<int>(state.entries.size()) - visible);
    state.scrollOffset = std::clamp(state.scrollOffset, 0, maxFirst);
}

void scrollToVisible(PleditState& state, std::size_t index) {
    const int visible = pleditVisibleTracks(state.heightSegments);
    const int row = static_cast<int>(index);
    if (row < state.scrollOffset) {
        state.scrollOffset = row;
    } else if (row >= state.scrollOffset + visible) {
        state.scrollOffset = row - visible + 1;
    }
    clampScroll(state);
}

void enqueueIndex(PleditState& state, std::size_t index) {
    if (queuePosition(state, index) == 0) {
        state.queue.push_back(index);
    }
}

void clearQueue(PleditState& state) { state.queue.clear(); }

void popQueueFront(PleditState& state) {
    if (!state.queue.empty()) {
        state.queue.erase(state.queue.begin());
    }
}

PleditGeometry pleditGeometry(const PleditState& state) {
    PleditGeometry geometry;
    geometry.width = pleditWidth(state);
    geometry.height = state.shade ? kPleditShadeHeight : pleditHeight(state);
    geometry.shade = state.shade;
    return geometry;
}

void applyPleditGeometry(PleditState& state, const PleditGeometry& geometry) {
    const int width = std::max(kPleditBaseWidth, geometry.width);
    const int height = std::max(kPleditBaseHeight, geometry.height);
    state.widthSegments = (width - kPleditBaseWidth) / kPleditSegmentWidth;
    state.heightSegments = (height - kPleditBaseHeight) / kPleditSegmentHeight;
    state.shade = geometry.shade;
    clampScroll(state);
}

}  // namespace koalaamp::ui
