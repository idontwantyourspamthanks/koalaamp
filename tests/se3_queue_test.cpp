// SPDX-License-Identifier: MIT
//
// SE-3 queue semantics through the real core: `PlaybackCoreTransport` is the
// `ITransport` the playlist drives, so this pins the queue verbs SE-3 relies on
// against FE-4's actual `PlaybackCore` (enqueue order, peek-next = front,
// shuffle leaves the queue order alone, clear). Compiled only where the
// libvlc-backed core exists (`koalaamp_ui_playback`).
#include <cstddef>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "playback/PlaybackCore.hpp"
#include "ui/PlaybackCoreTransport.hpp"
#include "ui/VisFeed.hpp"

namespace {

using koalaamp::playback::MediaRef;
using koalaamp::playback::PlaybackConfig;
using koalaamp::playback::PlaybackCore;
using koalaamp::ui::PlaybackCoreTransport;
using koalaamp::ui::VisFeed;

MediaRef media(const std::string& path) { return MediaRef{MediaRef::Kind::Path, path}; }

}  // namespace

TEST_CASE("SE-3 drives the real PlaybackCore queue through ITransport",
          "[se3][queue]") {
    PlaybackConfig config;
    config.output = PlaybackConfig::Output::Null;  // no device: queue-only exercise
    PlaybackCore core(config);
    VisFeed feed;
    PlaybackCoreTransport transport(core, feed);

    CHECK(transport.queueSize() == 0);
    CHECK_FALSE(transport.peekNext().has_value());

    // Enqueue order is FIFO; peekNext is the queue front.
    transport.enqueue(media("/music/a.mp3"));
    transport.enqueue(media("/music/b.mp3"));
    transport.enqueue(media("/music/c.mp3"));
    CHECK(transport.queueSize() == 3);
    REQUIRE(transport.peekNext().has_value());
    CHECK(transport.peekNext()->value == "/music/a.mp3");

    // Shuffle must not reorder the queue: it is consumed before any shuffle
    // refill (FE-4's core deque semantics).
    transport.setShuffle(true);
    CHECK(transport.shuffle());
    REQUIRE(transport.peekNext().has_value());
    CHECK(transport.peekNext()->value == "/music/a.mp3");
    transport.setShuffle(false);

    transport.clearQueue();
    CHECK(transport.queueSize() == 0);
    CHECK_FALSE(transport.peekNext().has_value());
}

int main(int argc, char** argv) {
    return Catch::Session().run(argc, argv);
}
