// SPDX-License-Identifier: MIT
//
// SE-3 pure-function tests: ATF formatting, the playlist model (selection,
// drag-reorder, queue view), the J-dialog fuzzy matcher and the geometry
// helpers. No Qt widgets, no skin fixture — these run everywhere.
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ui/Atf.hpp"
#include "ui/PleditRenderer.hpp"
#include "ui/JumpToFileDialog.hpp"
#include "ui/PleditState.hpp"

namespace {

using koalaamp::playback::MediaRef;
using namespace koalaamp::ui;

PleditEntry entry(const std::string& title) {
    PleditEntry e;
    e.title = title;
    e.filename = "/music/" + title + ".mp3";
    e.lengthMs = 61'000;
    return e;
}

PleditState stateWith(std::initializer_list<const char*> titles) {
    PleditState state;
    for (const char* title : titles) {
        state.entries.push_back(entry(title));
    }
    return state;
}

}  // namespace

TEST_CASE("SE-3 ATF renders the default title format", "[se3][atf]") {
    CHECK(defaultPlaylistTitle(MediaRef{MediaRef::Kind::Path, "/music/song.mp3"}) == "song");
    CHECK(defaultPlaylistTitle(MediaRef{MediaRef::Kind::Path, "C:\\albums\\A - B.flac"}) ==
          "A - B");
    CHECK(defaultPlaylistTitle(MediaRef{MediaRef::Kind::Url, "http://host/a/b.ogg?x=1"}) == "b");

    // Real tags win over the filename fallback.
    AtfFields fields;
    fields.artist = "Artist";
    fields.title = "Title";
    fields.filename = "/music/song.mp3";
    CHECK(formatAtf(kDefaultAtf, fields) == "Artist - Title");

    // No artist: the optional block vanishes entirely (no leading " - ").
    fields.artist.clear();
    CHECK(formatAtf(kDefaultAtf, fields) == "Title");

    // No title: `$if2` falls through to `$filepart(%filename%)`.
    fields.title.clear();
    CHECK(formatAtf(kDefaultAtf, fields) == "song.mp3");

    // Both tags present: `%artist% - %title%`.
    AtfFields both;
    both.artist = "A";
    both.title = "T";
    CHECK(formatAtf("%artist% - %title%", both) == "A - T");
    CHECK(formatAtf("%album%", both).empty());
    CHECK(formatAtf("literal %unknown% text", both) == "literal  text");

    // Nested functions and empty-first `$if2` argument.
    both.album = "Rec";
    CHECK(formatAtf("$if2(%album%,%artist%)", both) == "Rec");
    CHECK(formatAtf("$if2(%missing%,$filepart(%filename%))",
                    AtfFields{{}, {}, {}, "/x/y/z.mp3"}) == "z.mp3");
}

TEST_CASE("SE-3 ATF is total on malformed input", "[se3][atf]") {
    AtfFields fields;
    fields.title = "T";
    CHECK(formatAtf("[%title%", fields) == "T");     // unterminated block kept
    CHECK(formatAtf("%title", fields) == "%title");  // lone percent stays literal
    CHECK(formatAtf("$filepart(%title%", fields) == "T");  // unterminated function
    CHECK(formatAtf("$if2(%title%)", fields) == "T");
    CHECK(formatAtf("", fields).empty());
}

TEST_CASE("SE-3 jump-to-file fuzzy ranking", "[se3][jump]") {
    // Substring beats subsequence; earlier beats later.
    CHECK(jumpMatchScore("", "anything") > 0);
    CHECK(jumpMatchScore("track", "Artist - Track 1") > 0);
    CHECK(jumpMatchScore("zzz", "Artist - Track 1") == 0);
    CHECK(jumpMatchScore("atrk", "artist - track") > 0);          // subsequence
    CHECK(jumpMatchScore("track", "aa track") > jumpMatchScore("track", "aaaaaaaa track"));

    const std::vector<PleditEntry> entries = {
        entry("Alpha"), entry("Beta"), entry("Track nine"), entry("Betamax")};
    const auto all = rankJumpMatches(entries, "");
    CHECK(all.size() == 4);
    const auto betas = rankJumpMatches(entries, "beta");
    REQUIRE(betas.size() == 2);
    // "Beta" matches at position 0; "Betamax" also substring — stable order.
    CHECK(betas[0] == 1);
    CHECK(betas[1] == 3);
    const auto none = rankJumpMatches(entries, "nope");
    CHECK(none.empty());
}

TEST_CASE("SE-3 playlist selection model", "[se3][model]") {
    PleditState state = stateWith({"a", "b", "c", "d"});
    CHECK_FALSE(isSelected(state, 0));
    selectSingle(state, 2);
    CHECK(isSelected(state, 2));
    CHECK(selectedCount(state) == 1);
    selectToggle(state, 2);
    CHECK_FALSE(isSelected(state, 2));  // toggled off
    selectToggle(state, 0);
    selectToggle(state, 2);
    CHECK(selectedCount(state) == 2);

    selectRange(state, 1, 3);
    CHECK(state.selected == std::vector<std::size_t>({1, 2, 3}));
    selectRange(state, 3, 1);
    CHECK(state.selected == std::vector<std::size_t>({1, 2, 3}));  // order-independent
    selectAll(state);
    CHECK(selectedCount(state) == 4);
    clearSelection(state);
    CHECK(selectedCount(state) == 0);
}

TEST_CASE("SE-3 drag-reorder moves rows and remaps the queue", "[se3][model]") {
    PleditState state = stateWith({"a", "b", "c", "d", "e"});
    enqueueIndex(state, 0);
    enqueueIndex(state, 2);  // queue: rows a, c
    state.currentIndex = 4;

    selectRange(state, 0, 1);      // move a, b to the end
    moveSelected(state, 5);
    const std::vector<std::string> order = {
        state.entries[0].title, state.entries[1].title, state.entries[2].title,
        state.entries[3].title, state.entries[4].title};
    CHECK(order == std::vector<std::string>({"c", "d", "e", "a", "b"}));
    // The moved block is now selected at its destination.
    CHECK(state.selected == std::vector<std::size_t>({3, 4}));
    // Queue order follows the queue, not the rows: a (row 3) is still first.
    REQUIRE(state.queue.size() == 2);
    CHECK(state.queue[0] == 3);
    CHECK(state.queue[1] == 0);
    CHECK(queuePosition(state, 3) == 1);
    CHECK(queuePosition(state, 0) == 2);
    CHECK(state.currentIndex == 2);  // e moved from 4 to 2

    // Drop in the middle: move the (currently selected) a,b before row index 1.
    moveSelected(state, 1);
    const std::vector<std::string> order2 = {
        state.entries[0].title, state.entries[1].title, state.entries[2].title,
        state.entries[3].title, state.entries[4].title};
    CHECK(order2 == std::vector<std::string>({"c", "a", "b", "d", "e"}));
    CHECK(state.queue[0] == 1);  // a
    CHECK(state.queue[1] == 0);  // c
}

TEST_CASE("SE-3 remove drops rows, queue entries and the current track", "[se3][model]") {
    PleditState state = stateWith({"a", "b", "c", "d"});
    enqueueIndex(state, 0);
    enqueueIndex(state, 1);
    enqueueIndex(state, 2);
    state.currentIndex = 1;

    selectToggle(state, 1);
    CHECK(removeSelected(state) == 1);
    REQUIRE(state.entries.size() == 3);
    CHECK(state.entries[0].title == "a");
    CHECK(state.entries[1].title == "c");
    REQUIRE(state.queue.size() == 2);
    CHECK(state.queue[0] == 0);  // a
    CHECK(state.queue[1] == 1);  // c
    CHECK(state.currentIndex == -1);  // the playing row was removed
    CHECK(selectedCount(state) == 0);
}

TEST_CASE("SE-3 queue view and geometry helpers", "[se3][model]") {
    PleditState state = stateWith({"a", "b", "c"});
    enqueueIndex(state, 2);
    enqueueIndex(state, 0);
    enqueueIndex(state, 2);  // duplicate ignored
    REQUIRE(state.queue.size() == 2);
    CHECK(queuePosition(state, 2) == 1);
    CHECK(queuePosition(state, 0) == 2);
    popQueueFront(state);
    CHECK(queuePosition(state, 0) == 1);
    clearQueue(state);
    CHECK(state.queue.empty());

    CHECK(pleditWidth(state) == 275);
    CHECK(pleditHeight(state) == 116);
    CHECK(pleditVisibleTracks(0) == 4);
    CHECK(pleditVisibleTracks(1) == 6);
    state.widthSegments = 3;
    state.heightSegments = 2;
    CHECK(pleditWidth(state) == 275 + 3 * 25);
    CHECK(pleditHeight(state) == 116 + 2 * 29);
    CHECK(pleditVisibleTracks(2) == 8);

    state.entries.resize(3);
    state.scrollOffset = 100;
    clampScroll(state);
    CHECK(state.scrollOffset == 0);

    PleditGeometry geometry = pleditGeometry(state);
    CHECK(geometry.width == 350);
    CHECK(geometry.height == 174);
    CHECK_FALSE(geometry.shade);
    geometry.shade = true;
    applyPleditGeometry(state, geometry);
    CHECK(state.shade);
    CHECK(state.widthSegments == 3);
    CHECK(state.heightSegments == 2);
}

TEST_CASE("SE-3 row duration formatting", "[se3][model]") {
    CHECK(formatPleditDuration(0) == "--:--");
    CHECK(formatPleditDuration(-5) == "--:--");
    CHECK(formatPleditDuration(1000) == "0:01");
    CHECK(formatPleditDuration(65'000) == "1:05");
    CHECK(formatPleditDuration(6'000'000) == "100:00");
}

namespace {

std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

}  // namespace

// The ticket's layering guard (docs/tickets/SE-3-playlist-window.md): SE-3
// drives `PlaybackCore` only through `ITransport`, so the UI reaches the engine
// exclusively via the `PlaybackCoreTransport` bridge, and the concurrent
// tickets' source trees stay UI-free. This is the same grep-the-tree pattern
// dsp_test (`src/audio`) and playback_core_test (`src/playback`) use.
TEST_CASE("SE-3 never reaches past the transport seam", "[se3][guard]") {
    const auto files = [](const std::filesystem::path& dir) {
        std::vector<std::filesystem::path> out;
        if (!std::filesystem::is_directory(dir)) {
            return out;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() &&
                (entry.path().extension() == ".cpp" || entry.path().extension() == ".hpp")) {
                out.push_back(entry.path());
            }
        }
        return out;
    };

    for (const char* dir : {KOALAAMP_PLAYBACK_SRC, KOALAAMP_INTEROP_SRC, KOALAAMP_AUDIO_SRC}) {
        for (const auto& path : files(dir)) {
            const std::string text = slurp(path);
            INFO("protected file: " << path.string());
            CHECK(text.find("#include \"ui/") == std::string::npos);
            CHECK(text.find("koalaamp::ui") == std::string::npos);
        }
    }

    for (const auto& path : files(KOALAAMP_UI_SRC)) {
        const std::string name = path.filename().string();
        if (name == "PlaybackCoreTransport.cpp" || name == "PlaybackCoreTransport.hpp") {
            continue;  // the sanctioned bridge
        }
        const std::string text = slurp(path);
        INFO("ui file: " << name);
        // Only the bridge may *include* the engine headers; prose may of course
        // mention PlaybackCore. libvlc must never enter this layer at all.
        CHECK(text.find("#include \"playback/PlaybackCore") == std::string::npos);
        CHECK(text.find("#include <vlc") == std::string::npos);
        CHECK(text.find("#include \"vlc") == std::string::npos);
        CHECK(text.find("#include <libvlc") == std::string::npos);
    }
}
