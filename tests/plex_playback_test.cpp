// SPDX-License-Identifier: MIT
// SV-4 — Plex timeline/watched reporting, profile headers, media source.

#include "playback/IAudioEngine.hpp"
#include "servers/jellyfin/FixtureTransport.hpp"
#include "servers/plex/PlexClient.hpp"
#include "servers/plex/PlexMediaSource.hpp"
#include "servers/plex/PlexTimeline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace koalaamp::servers::plex;
using koalaamp::playback::MediaRef;
using koalaamp::playback::ServerMediaInfo;
using koalaamp::servers::jellyfin::FixtureTransport;
using koalaamp::servers::jellyfin::HttpRequest;
using koalaamp::servers::jellyfin::HttpResponse;
using koalaamp::servers::jellyfin::IHttpTransport;

constexpr const char* kFixtureDir = KOALAAMP_PLEX_FIXTURE_DIR;
const std::string kPms = "http://plex.local:32400";
const std::string kToken = "pms-token";

PlexClientInfo clientInfo() {
    return PlexClientInfo{"client-id", "KoalaAmp", "0.0.1", "tester", "Generic"};
}

PlexSession session() {
    return PlexSession{kPms, kToken, "server-1", "user-uuid", "tester"};
}

class CapturingTransport final : public IHttpTransport {
public:
    HttpResponse perform(const HttpRequest& request,
                         const koalaamp::library::Cancellation&) override {
        requests.push_back(request);
        HttpResponse response;
        response.status = 200;
        return response;
    }

    std::vector<HttpRequest> requests;
};

const std::string* headerValue(const HttpRequest& request, const std::string& name) {
    for (const auto& [key, value] : request.headers) {
        if (key == name) {
            return &value;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("timeline, scrobble and unscrobble use the koalapaw query shapes") {
    CapturingTransport transport;
    PlexTimeline timeline{transport, clientInfo(), session()};

    CHECK(timeline.report(PlexTimelineEntry{"200", "/library/metadata/200",
                                            PlexTimelineState::Playing, 1234, 180000}));
    REQUIRE(transport.requests.size() == 1);
    CHECK(transport.requests[0].method == "POST");
    CHECK(transport.requests[0].url ==
          kPms + "/:/timeline?ratingKey=200&key=%2Flibrary%2Fmetadata%2F200"
                 "&state=playing&time=1234&duration=180000");
    CHECK(*headerValue(transport.requests[0], "X-Plex-Token") == kToken);
    CHECK(transport.requests[0].url.find(kToken) == std::string::npos);

    CHECK(timeline.scrobble("200"));
    CHECK(timeline.unscrobble("200"));
    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[1].method == "GET");
    CHECK(transport.requests[1].url ==
          kPms + "/:/scrobble?key=200&identifier=com.plexapp.plugins.library");
    CHECK(transport.requests[2].url ==
          kPms + "/:/unscrobble?key=200&identifier=com.plexapp.plugins.library");
}

TEST_CASE("the reporter sends start, pause, heartbeat and stop") {
    CapturingTransport transport;
    PlexPlaybackReporter reporter{transport, clientInfo(), session()};

    MediaRef media;
    media.kind = MediaRef::Kind::Stream;
    media.server = ServerMediaInfo{"200", {}, "DirectPlay"};
    reporter.onTrackChanged(media);
    reporter.tick(1000);
    REQUIRE(transport.requests.size() == 1);
    CHECK(transport.requests[0].url.find("state=playing") != std::string::npos);
    CHECK(transport.requests[0].url.find("time=0") != std::string::npos);

    reporter.onPositionChanged(5000, 180000);
    reporter.onStateChanged(koalaamp::playback::PlaybackState::Paused);
    reporter.tick(1100);
    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[1].url.find("state=paused") != std::string::npos);
    CHECK(transport.requests[1].url.find("time=5000") != std::string::npos);
    CHECK(transport.requests[1].url.find("duration=180000") != std::string::npos);

    reporter.onStateChanged(koalaamp::playback::PlaybackState::Playing);
    reporter.tick(1200);
    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[2].url.find("state=playing") != std::string::npos);

    // 10 s heartbeat (the first tick armed the clock at 1000 ms).
    reporter.tick(10999);
    CHECK(transport.requests.size() == 3);
    reporter.tick(11000);
    REQUIRE(transport.requests.size() == 4);

    reporter.onStateChanged(koalaamp::playback::PlaybackState::Stopped);
    reporter.tick(11100);
    REQUIRE(transport.requests.size() == 5);
    CHECK(transport.requests[4].url.find("state=stopped") != std::string::npos);

    CHECK(reporter.markPlayed());
    CHECK(reporter.markUnplayed());
    CHECK(transport.requests.back().url.find("/:/unscrobble") != std::string::npos);
    CHECK(reporter.stats().reports == 5);
}

TEST_CASE("a local track closes the Plex timeline without starting another") {
    CapturingTransport transport;
    PlexPlaybackReporter reporter{transport, clientInfo(), session()};

    MediaRef remote;
    remote.server = ServerMediaInfo{"200", {}, "DirectPlay"};
    reporter.onTrackChanged(remote);
    reporter.tick(0);

    MediaRef local;
    local.kind = MediaRef::Kind::Path;
    local.value = "/music/song.flac";
    reporter.onTrackChanged(local);
    reporter.tick(10);

    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[1].url.find("state=stopped") != std::string::npos);
    CHECK(reporter.currentRatingKey().empty());
}

TEST_CASE("metadata requests advertise the direct-play profile") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    PlexClient client{transport, clientInfo(), kPms, kToken};
    REQUIRE(client.identity().has_value());
    REQUIRE(!transport.requests().empty());
    const auto* profile = headerValue(transport.requests().front(), "X-Plex-Client-Profile-Extra");
    REQUIRE(profile != nullptr);
    CHECK(profile->find("type=musicProfile") != std::string::npos);
    CHECK(profile->find("audioCodec=flac") != std::string::npos);
    CHECK(*headerValue(transport.requests().front(), "X-Plex-Platform") == "Generic");

    CHECK(plexVersionAtLeast("1.41.0", "1.40.0"));
    CHECK_FALSE(plexVersionAtLeast("1.39.9", "1.40.0"));
    CHECK(plexVersionAtLeast("1.41", "1.41.0"));
}

TEST_CASE("the media source opens a part, reads and seeks with Range") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    PlexMediaSource source{transport, clientInfo(), kPms, kToken,
                           "/library/parts/555/123/file.flac"};
    std::int64_t size = -1;
    auto stream = source.open(&size);
    REQUIRE(stream != nullptr);
    CHECK(size == 10);

    unsigned char first[5] = {};
    CHECK(stream->read(first, 4) == 4);
    CHECK(std::string(reinterpret_cast<char*>(first), 4) == "0123");
    REQUIRE(stream->seek(20));
    unsigned char later[5] = {};
    CHECK(stream->read(later, 4) == 4);
    CHECK(std::string(reinterpret_cast<char*>(later), 4) == "abcd");

    REQUIRE(transport.requests().size() == 2);
    CHECK(transport.requests()[1].url == kPms + "/library/parts/555/123/file.flac");
    CHECK(*headerValue(transport.requests()[1], "Range") == "bytes=20-");

    koalaamp::library::Track track;
    track.id = "200";
    track.title = "First Track";
    track.path = "/library/parts/555/123/file.flac";
    const MediaRef media = makePlexMedia(transport, clientInfo(), kPms, kToken, track);
    CHECK(media.kind == MediaRef::Kind::Stream);
    REQUIRE(media.stream != nullptr);
    REQUIRE(media.server.has_value());
    CHECK(media.server->itemId == "200");
    CHECK(media.server->playMethod == "DirectPlay");
}
