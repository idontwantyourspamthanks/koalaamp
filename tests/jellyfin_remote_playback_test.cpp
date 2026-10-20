// SPDX-License-Identifier: MIT
// SV-2 — remote commands against the real PlaybackCore queue. Gated on libvlc
// with the rest of the FE-4 core tests.

#include "playback/PlaybackCore.hpp"
#include "servers/jellyfin/JellyfinRemotePlayback.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

namespace {

using koalaamp::playback::MediaRef;
using koalaamp::playback::PlaybackConfig;
using koalaamp::playback::PlaybackCore;
using koalaamp::playback::PlaybackState;
using koalaamp::servers::jellyfin::PlaybackRemoteAdapter;
using koalaamp::servers::jellyfin::RemoteTrackRef;

std::optional<MediaRef> resolveToPath(const RemoteTrackRef& item) {
    MediaRef media;
    media.kind = MediaRef::Kind::Path;
    media.value = "/music/" + item.id + ".flac";
    return media;
}

} // namespace

TEST_CASE("PlayNext queues ahead without starting or hijacking playback") {
    PlaybackConfig config;
    config.output = PlaybackConfig::Output::Null;
    PlaybackCore core{config};
    PlaybackRemoteAdapter adapter{core, resolveToPath};

    adapter.playLast({RemoteTrackRef{"first", "First"}});
    adapter.playNext({RemoteTrackRef{"second", "Second"}});

    CHECK(core.state() == PlaybackState::Stopped);
    CHECK(core.queueSize() == 2);
    const auto next = core.peekNext();
    REQUIRE(next.has_value());
    CHECK(next->value == "/music/second.flac");
}

TEST_CASE("the adapter maps transport verbs onto the core") {
    PlaybackConfig config;
    config.output = PlaybackConfig::Output::Null;
    PlaybackCore core{config};
    PlaybackRemoteAdapter adapter{core, resolveToPath};

    adapter.setVolume(65);
    CHECK(core.volume100() == 65);

    // Nothing is loaded: these must be safe no-ops, not crashes.
    adapter.pause();
    adapter.unpause();
    adapter.pause();
    adapter.seekMs(5000);
    adapter.nextTrack();
    adapter.previousTrack();
    adapter.playPause();
    adapter.stop();

    CHECK(core.state() == PlaybackState::Stopped);
}

TEST_CASE("an unresolvable cast item is dropped, not queued as garbage") {
    PlaybackConfig config;
    config.output = PlaybackConfig::Output::Null;
    PlaybackCore core{config};
    PlaybackRemoteAdapter adapter{core, [](const RemoteTrackRef&) -> std::optional<MediaRef> {
                                      return std::nullopt;
                                  }};

    adapter.playNext({RemoteTrackRef{"missing", "Missing"}});
    CHECK(core.queueSize() == 0);
}
