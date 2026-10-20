// SPDX-License-Identifier: MIT
// SV-2 — direct/transcode planning and `/Sessions/Playing*` player wiring.
//
// Reports are captured by a recording transport so the test can assert the wire
// payload and the 10 s cadence deterministically; stream URLs go through the
// fixture transport, the same path production uses.

#include "servers/jellyfin/FixtureTransport.hpp"
#include "servers/jellyfin/JellyfinClient.hpp"
#include "servers/jellyfin/JellyfinPlayback.hpp"
#include "servers/jellyfin/JellyfinPlaybackReporter.hpp"

#include <nlohmann/json.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace koalaamp::servers::jellyfin;
using koalaamp::library::Rg;
using koalaamp::library::SourceId;
using koalaamp::library::Track;
using koalaamp::playback::MediaRef;
using koalaamp::playback::ServerMediaInfo;

constexpr const char* kFixtureDir = KOALAAMP_FIXTURE_DIR;
const std::string kBaseUrl = "http://jellyfin.local:8096";
const std::string kToken = "fixture-token-1";
const std::string kDeviceId = "device-under-test";
const std::string kItemId = "70000000000000000000000000000002";

DeviceProfile testProfile() {
    DeviceProfile profile;
    profile.clientName = "KoalaAmp";
    profile.deviceName = kDeviceId;
    profile.clientVersion = "0.0.1";
    return profile;
}

Track trackWith(const std::string& codec, std::int64_t bitrate = 0) {
    Track track;
    track.src = SourceId::Jellyfin;
    track.id = kItemId;
    track.title = "Pink + White";
    track.codec = codec;
    track.bitrate = bitrate;
    return track;
}

MediaRef serverMedia(const std::string& itemId, const std::string& method = "DirectPlay") {
    MediaRef media;
    media.kind = MediaRef::Kind::Stream;
    media.value = "track";
    media.server = ServerMediaInfo{itemId, {}, method};
    return media;
}

nlohmann::json bodyOf(const koalaamp::servers::jellyfin::HttpRequest& request) {
    return nlohmann::json::parse(request.body);
}

class CapturingTransport final : public IHttpTransport {
public:
    HttpResponse perform(const HttpRequest& request,
                         const koalaamp::library::Cancellation&) override {
        requests.push_back(request);
        HttpResponse response;
        response.status = 204;
        return response;
    }

    std::vector<HttpRequest> requests;
};

} // namespace

TEST_CASE("direct play is refused for unknown codecs and over-budget bitrates") {
    PlaybackPolicy policy;
    CHECK(policy.canDirectPlay("flac", 0));
    CHECK(policy.canDirectPlay("FLAC", 1'411'200));
    CHECK(policy.canDirectPlay("", 0)); // unknown metadata: do not transcode blindly
    CHECK(policy.canDirectPlay("m4a,aac", 0));
    CHECK_FALSE(policy.canDirectPlay("dsf", 0));
    CHECK_FALSE(policy.canDirectPlay("flac", policy.maxDirectBitrate + 1));
}

TEST_CASE("flac plans direct play over the static endpoint") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    JellyfinPlayback playback{transport, testProfile()};

    Track track = trackWith("flac");
    track.id = "77777777777777777777777777777777";
    Rg rg;
    rg.trackGain = -6.2;
    track.rg = rg;

    const PlaybackPlan plan = playback.plan(kBaseUrl, kToken, kDeviceId, track);
    CHECK(plan.method == PlayMethod::DirectPlay);
    CHECK(plan.spec.directPlay);
    CHECK(plan.spec.itemId == track.id);
    CHECK(plan.spec.playSessionId.empty());
    REQUIRE(plan.media.server.has_value());
    CHECK(plan.media.server->itemId == track.id);
    CHECK(plan.media.server->playMethod == std::string("DirectPlay"));
    REQUIRE(plan.media.rg.has_value());
    CHECK(plan.media.rg->trackGain == -6.2);
    CHECK(plan.media.kind == MediaRef::Kind::Stream);
    CHECK(plan.media.value == "Pink + White");

    std::int64_t size = -1;
    const auto stream = plan.media.stream->open(&size);
    REQUIRE(stream != nullptr);
    CHECK(size == 10); // fixture 11's Content-Length
    REQUIRE(transport.requests().size() == 1);
    CHECK(transport.requests()[0].url ==
          kBaseUrl + "/Audio/" + track.id + "/stream?static=true");
}

TEST_CASE("an unsupported codec plans a progressive transcode, not an HLS URL") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    JellyfinPlayback playback{transport, testProfile()};

    const PlaybackPlan plan =
        playback.plan(kBaseUrl, kToken, kDeviceId, trackWith("dsf"), "abc-session");
    CHECK(plan.method == PlayMethod::Transcode);
    CHECK_FALSE(plan.spec.directPlay);
    CHECK(plan.spec.audioCodec == "aac");
    CHECK(plan.spec.audioBitRate == 192000);
    CHECK(plan.spec.maxAudioChannels == 2);
    CHECK(plan.spec.playSessionId == "abc-session");
    REQUIRE(plan.media.server.has_value());
    CHECK(plan.media.server->playMethod == std::string("Transcode"));

    std::int64_t size = -1;
    const auto stream = plan.media.stream->open(&size);
    REQUIRE(stream != nullptr);
    CHECK(size == 15);
    REQUIRE(transport.requests().size() == 1);
    // The token is still a header, never a query parameter.
    CHECK(transport.requests()[0].url ==
          kBaseUrl + "/Audio/" + kItemId +
              "/stream?static=false&audioCodec=aac&audioBitRate=192000&maxAudioChannels=2"
              "&playSessionId=abc-session");
    CHECK(transport.requests()[0].url.find("api_key") == std::string::npos);
    CHECK(transport.requests()[0].url.find(kToken) == std::string::npos);
}

TEST_CASE("a bitrate above the policy cap forces transcode") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    PlaybackPolicy policy;
    JellyfinPlayback playback{transport, testProfile(), policy};

    const PlaybackPlan plan = playback.plan(kBaseUrl, kToken, kDeviceId,
                                            trackWith("flac", policy.maxDirectBitrate + 1));
    CHECK(plan.method == PlayMethod::Transcode);
    CHECK_FALSE(plan.spec.directPlay);
}

TEST_CASE("the reporter emits Start, pause/seek/volume progress and Stop") {
    CapturingTransport transport;
    JellyfinClient client{transport, testProfile()};
    JellyfinPlaybackReporter reporter{client, kBaseUrl, kToken, kDeviceId};
    const std::string session = reporter.playSessionId();

    reporter.onTrackChanged(serverMedia("item-1"));
    reporter.tick(1000);
    REQUIRE(transport.requests.size() == 1);
    CHECK(transport.requests[0].url == kBaseUrl + "/Sessions/Playing");
    nlohmann::json start = bodyOf(transport.requests[0]);
    CHECK(start["ItemId"] == "item-1");
    CHECK(start["PlayMethod"] == "DirectPlay");
    CHECK(start["PlaySessionId"] == session);
    CHECK(start["IsPaused"] == false);
    CHECK(start["MediaSourceId"] == "item-1");
    CHECK(start["PositionTicks"] == 0);

    reporter.onStateChanged(koalaamp::playback::PlaybackState::Paused);
    reporter.tick(1200);
    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[1].url == kBaseUrl + "/Sessions/Playing/Progress");
    nlohmann::json pause = bodyOf(transport.requests[1]);
    CHECK(pause["EventName"] == "Pause");
    CHECK(pause["IsPaused"] == true);

    // A backward jump of 4 s is a seek, not playback drift.
    reporter.onPositionChanged(5000, 200000);
    reporter.onPositionChanged(1000, 200000);
    reporter.tick(1400);
    REQUIRE(transport.requests.size() == 3);
    nlohmann::json seek = bodyOf(transport.requests[2]);
    CHECK(seek["EventName"] == "Seek");
    CHECK(seek["PositionTicks"] == 1000 * 10000);

    reporter.onStateChanged(koalaamp::playback::PlaybackState::Playing);
    reporter.tick(1500); // Unpause is an event report
    REQUIRE(transport.requests.size() == 4);
    CHECK(bodyOf(transport.requests[3])["EventName"] == "Unpause");

    // 10 s cadence: 9.999 s after the first tick is not due, 10 s is.
    reporter.tick(10999);
    CHECK(transport.requests.size() == 4);
    reporter.tick(11000);
    REQUIRE(transport.requests.size() == 5);
    nlohmann::json heartbeat = bodyOf(transport.requests[4]);
    CHECK(heartbeat["EventName"] == "TimeUpdate");
    CHECK(heartbeat["PositionTicks"] == 1000 * 10000);

    reporter.onStateChanged(koalaamp::playback::PlaybackState::Stopped);
    reporter.tick(11500);
    REQUIRE(transport.requests.size() == 6);
    CHECK(transport.requests[5].url == kBaseUrl + "/Sessions/Playing/Stopped");
    nlohmann::json stop = bodyOf(transport.requests[5]);
    CHECK(stop["ItemId"] == "item-1");
    CHECK(stop["PlaySessionId"] == session);

    // A stop ends the queue session: the next one gets a fresh id.
    CHECK(reporter.playSessionId() != session);
}

TEST_CASE("volume changes carry the level in the report") {
    CapturingTransport transport;
    JellyfinClient client{transport, testProfile()};
    JellyfinPlaybackReporter reporter{client, kBaseUrl, kToken, kDeviceId};

    reporter.onTrackChanged(serverMedia("item-1"));
    reporter.tick(0);
    reporter.onVolumeChanged(42);
    reporter.tick(10);

    REQUIRE(transport.requests.size() == 2);
    nlohmann::json volume = bodyOf(transport.requests[1]);
    CHECK(volume["EventName"] == "VolumeChange");
    CHECK(volume["VolumeLevel"] == 42);
}

TEST_CASE("a gapless track change keeps the queue session id") {
    CapturingTransport transport;
    JellyfinClient client{transport, testProfile()};
    JellyfinPlaybackReporter reporter{client, kBaseUrl, kToken, kDeviceId};

    reporter.onTrackChanged(serverMedia("item-1"));
    reporter.tick(0);
    REQUIRE(transport.requests.size() == 1);
    const std::string session = bodyOf(transport.requests[0])["PlaySessionId"];

    reporter.onTrackChanged(serverMedia("item-2"));
    reporter.tick(10);
    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[1].url == kBaseUrl + "/Sessions/Playing/Stopped");
    CHECK(bodyOf(transport.requests[1])["ItemId"] == "item-1");
    CHECK(bodyOf(transport.requests[2])["ItemId"] == "item-2");
    CHECK(bodyOf(transport.requests[2])["PlayMethod"] == "DirectPlay");
    CHECK(bodyOf(transport.requests[2])["PlaySessionId"] == session);
}

TEST_CASE("a local track ends the server session without starting a new one") {
    CapturingTransport transport;
    JellyfinClient client{transport, testProfile()};
    JellyfinPlaybackReporter reporter{client, kBaseUrl, kToken, kDeviceId};

    reporter.onTrackChanged(serverMedia("item-1"));
    reporter.tick(0);

    MediaRef local;
    local.kind = MediaRef::Kind::Path;
    local.value = "/music/song.flac";
    reporter.onTrackChanged(local);
    reporter.tick(10);

    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[1].url == kBaseUrl + "/Sessions/Playing/Stopped");
    CHECK(reporter.stats().starts == 1);
    CHECK(reporter.stats().stops == 1);
}
