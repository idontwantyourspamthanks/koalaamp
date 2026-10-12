// SPDX-License-Identifier: MIT
//
// PleditState — the complete renderable state of the classic Winamp playlist
// editor (docs/spec.md §5.4, docs/tickets/SE-3-playlist-window.md).
//
// Like `PlayerState`, this is a plain value type the renderer is a pure
// function of: entries, the selection set, the current (playing) row, the
// window's own queue view, scroll, shade and the 25x29 segment tiling size.
// The window owns one of these; the golden harness builds them by hand.
//
// Layout constants are ported from Webamp (MIT, `js/constants.ts` @
// webamp@e6283c0eabe9b1313cd41df16d2875e897719696): WINDOW_RESIZE_SEGMENT_WIDTH
// = 25, WINDOW_RESIZE_SEGMENT_HEIGHT = 29, WINDOW_WIDTH = 275,
// WINDOW_HEIGHT = 116, TRACK_HEIGHT = 13. Attribution in LICENSES.md.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace koalaamp::ui {

// --- geometry constants (1x skin pixels) ------------------------------------
inline constexpr int kPleditBaseWidth = 275;
inline constexpr int kPleditBaseHeight = 116;
inline constexpr int kPleditShadeHeight = 14;
inline constexpr int kPleditSegmentWidth = 25;   ///< horizontal resize tile
inline constexpr int kPleditSegmentHeight = 29;  ///< vertical resize tile
inline constexpr int kPleditTrackHeight = 13;    ///< one list row
inline constexpr int kPleditTopHeight = 20;      ///< title bar band
inline constexpr int kPleditBottomHeight = 38;   ///< menu/vis/grip band
inline constexpr int kPleditLeftWidth = 12;      ///< middle left tile strip
inline constexpr int kPleditRightWidth = 20;     ///< middle right tile strip
inline constexpr int kPleditListPad = 3;         ///< list top/bottom padding
/// Middle band height at size 0; the visible-track count is floor(middle/13),
/// which reproduces Webamp's `BASE_WINDOW_HEIGHT = 58` (4 rows at 275x116).
inline constexpr int kPleditListBase = 58;

/// One playlist row: the already ATF-formatted display title, the media the
/// transport plays, and its length (0 = unknown).
struct PleditEntry {
    std::string title;
    std::string filename;
    int lengthMs = 0;
    bool operator==(const PleditEntry&) const = default;
};

/// Everything the playlist window paints (and the drag/selection it is in).
struct PleditState {
    std::vector<PleditEntry> entries;
    /// Selected row indices, sorted and unique.
    std::vector<std::size_t> selected;
    /// Playing row, or -1.
    int currentIndex = -1;
    /// Row indices in queue order: the front is position 1. The window is the
    /// authority for which rows are queued (PlaybackCore only exposes the queue
    /// size and its front); it reconciles the size on every transport sync.
    std::vector<std::size_t> queue;
    /// Index of the first visible row.
    int scrollOffset = 0;
    /// Row under the pointer (highlight), or -1.
    int hoverIndex = -1;
    bool shade = false;
    bool focused = true;          ///< window focused: selected title-bar art
    int widthSegments = 0;        ///< extra 25 px tiles beyond 275
    int heightSegments = 0;       ///< extra 29 px tiles beyond 116
    /// Pointer state the renderer mirrors (pressed title-bar buttons / grip).
    bool closePressed = false;
    bool shadePressed = false;
    bool resizeActive = false;
};

/// Content size in 1x skin pixels.
inline int pleditWidth(const PleditState& state) {
    return kPleditBaseWidth + state.widthSegments * kPleditSegmentWidth;
}
inline int pleditHeight(const PleditState& state) {
    return kPleditBaseHeight + state.heightSegments * kPleditSegmentHeight;
}
/// Middle band height (everything between the top and bottom bands).
inline int pleditMiddleHeight(int heightSegments) {
    return kPleditListBase + heightSegments * kPleditSegmentHeight;
}
/// Rows visible at a given vertical segment count.
inline int pleditVisibleTracks(int heightSegments) {
    return pleditMiddleHeight(heightSegments) / kPleditTrackHeight;
}

// --- selection model (pure; classic Winamp single/ctrl/shift semantics) -----

bool isSelected(const PleditState& state, std::size_t index);
/// 1-based queue position of a row, or 0 when it is not queued.
std::size_t queuePosition(const PleditState& state, std::size_t index);

/// Replaces the selection with just `index`.
void selectSingle(PleditState& state, std::size_t index);
/// Ctrl-click: toggles `index` in/out of the selection.
void selectToggle(PleditState& state, std::size_t index);
/// Shift-click: replaces the selection with the inclusive range [anchor,index].
void selectRange(PleditState& state, std::size_t anchor, std::size_t index);
void clearSelection(PleditState& state);
void selectAll(PleditState& state);
/// Rough default selection when the list is loaded into the queue.
std::size_t selectedCount(const PleditState& state);

// --- list mutation ----------------------------------------------------------

/// Removes every selected row (and its queue entries). Returns how many went.
std::size_t removeSelected(PleditState& state);
/// Drag-reorder: moves the selected rows so they land before the row currently
/// at `targetIndex` (clamped to the end). Selection, queue and currentIndex are
/// remapped; the moved block stays selected.
void moveSelected(PleditState& state, std::size_t targetIndex);
/// Appends entries (the add-file path) and clears nothing else.
void appendEntries(PleditState& state, std::vector<PleditEntry> entries);
/// Keeps `scrollOffset` inside [0, maxFirstRow].
void clampScroll(PleditState& state);
/// Scrolls so `index` is visible, minimally.
void scrollToVisible(PleditState& state, std::size_t index);

// --- queue view -------------------------------------------------------------

/// Appends `index` to the queue view when not already queued.
void enqueueIndex(PleditState& state, std::size_t index);
void clearQueue(PleditState& state);
/// Drops the queue front (a queued track started playing).
void popQueueFront(PleditState& state);

// --- persistence helpers (docs/spec.md §5.3, SkinDb layout record) ----------
/// Window geometry for the SkinDb record; `playlistHeight` is the pixel height.
struct PleditGeometry {
    int width = kPleditBaseWidth;
    int height = kPleditBaseHeight;
    bool shade = false;
    bool operator==(const PleditGeometry&) const = default;
};
PleditGeometry pleditGeometry(const PleditState& state);
/// Applies a stored geometry, rounding to the 25/29 segment grid.
void applyPleditGeometry(PleditState& state, const PleditGeometry& geometry);

}  // namespace koalaamp::ui
