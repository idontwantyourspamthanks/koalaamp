// SPDX-License-Identifier: MIT
// SV-5 — server browse gateway: libraries, album contents, read-only playlists,
// rating, and the IServerSource seam. Fixture-replayed, no server needed.

#include "servers/jellyfin/FixtureTransport.hpp"
#include "servers/jellyfin/JellyfinClient.hpp"
#include "servers/jellyfin/JellyfinSource.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace koalaamp::servers::jellyfin;
using koalaamp::library::Query;
using koalaamp::library::StreamIntent;
using koalaamp::library::Track;
using koalaamp::servers::ServerAuth;

constexpr const char* kFixtureDir = KOALAAMP_FIXTURE_DIR;
const std::string kBaseUrl = "http://jellyfin.local:8096";
const std::string kToken = "fixture-token-1";
const std::string kDeviceId = "device-under-test";
const std::string kUser = "user-1";

DeviceProfile testProfile() {
    DeviceProfile profile;
    profile.clientName = "KoalaAmp";
    profile.deviceName = kDeviceId;
    profile.clientVersion = "0.0.1";
    return profile;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("music libraries come from the user's views") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    JellyfinClient client{transport, testProfile()};

    const LibrariesOutcome outcome =
        client.musicLibraries(kBaseUrl, kToken, kDeviceId, kUser);
    const auto* success = std::get_if<LibrariesSuccess>(&outcome);
    REQUIRE(success != nullptr);
    REQUIRE(success->libraries.size() == 2);
    CHECK(success->libraries[0].id == "lib-music");
    CHECK(success->libraries[0].name == "Music");
    CHECK(success->libraries[0].collectionType == "music");
    CHECK(success->libraries[1].collectionType == "movies");

    REQUIRE(transport.requests().size() == 1);
    CHECK(transport.requests()[0].url == kBaseUrl + "/Users/user-1/Views");
}

TEST_CASE("album contents arrive in disc/track order with codec and gain") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    JellyfinClient client{transport, testProfile()};

    const TracksOutcome outcome = client.albumTracks(kBaseUrl, kToken, kDeviceId, "album-1");
    const auto* success = std::get_if<TracksSuccess>(&outcome);
    REQUIRE(success != nullptr);
    REQUIRE(success->tracks.size() == 2);
    CHECK(success->tracks[0].id == "track-1");
    CHECK(success->tracks[0].title == "First");
    CHECK(success->tracks[0].trackNo == 1);
    CHECK(success->tracks[0].codec == "flac");
    REQUIRE(success->tracks[0].rg.has_value());
    CHECK(success->tracks[0].rg->trackGain == -5.0);
    CHECK(success->tracks[1].id == "track-2");
    CHECK(success->tracks[1].trackNo == 2);

    REQUIRE(transport.requests().size() == 1);
    CHECK(contains(transport.requests()[0].url, "/Items?parentId=album-1"));
    CHECK(contains(transport.requests()[0].url, "includeItemTypes=Audio"));
    CHECK(contains(transport.requests()[0].url, "sortBy=ParentIndexNumber,IndexNumber"));
}

TEST_CASE("playlists list and open read-only in playlist order") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    JellyfinClient client{transport, testProfile()};

    const NamedItemsOutcome lists = client.playlists(kBaseUrl, kToken, kDeviceId);
    const auto* list = std::get_if<NamedItemsSuccess>(&lists);
    REQUIRE(list != nullptr);
    REQUIRE(list->items.size() == 2);
    CHECK(list->items[0].id == "pl-1");
    CHECK(list->items[0].name == "Road Trip");
    CHECK(list->items[1].name == "Focus");

    const TracksOutcome items = client.playlistItems(kBaseUrl, kToken, kDeviceId, "pl-1", kUser);
    const auto* tracks = std::get_if<TracksSuccess>(&items);
    REQUIRE(tracks != nullptr);
    REQUIRE(tracks->tracks.size() == 2);
    CHECK(tracks->tracks[0].id == "pt-1");
    CHECK(tracks->tracks[1].id == "pt-2");

    REQUIRE(transport.requests().size() == 2);
    CHECK(transport.requests()[1].url ==
          kBaseUrl + "/Playlists/pl-1/Items?userId=user-1&fields="
                     "SortName,RunTimeTicks,Album,AlbumArtist,Artists,ProductionYear,"
                     "IndexNumber,ParentIndexNumber,Container,NormalizationGain");
}

TEST_CASE("rating posts the 0..10 value to the user-data endpoint") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    JellyfinClient client{transport, testProfile()};

    const ReportOutcome outcome =
        client.setRating(kBaseUrl, kToken, kDeviceId, kUser, "item-1", 4);
    CHECK(std::holds_alternative<ReportSuccess>(outcome));

    REQUIRE(transport.requests().size() == 1);
    CHECK(transport.requests()[0].url ==
          kBaseUrl + "/Users/user-1/Items/item-1/UserData");
    CHECK(contains(transport.requests()[0].body, "\"Rating\":8.0"));
}

TEST_CASE("the source searches, streams and rates through the same session") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    JellyfinSource source{transport, testProfile(), ServerAuth{kBaseUrl, kUser, kToken, kDeviceId}};
    CHECK(source.isAuthenticated());

    Query query;
    query.text = "pink";
    query.limit = 50;
    NoCancellation cancel;
    const std::vector<Track> tracks = source.search(query, cancel);
    REQUIRE(tracks.size() == 3);
    CHECK(tracks[0].id == "70000000000000000000000000000001");
    CHECK(source.lastError().empty());

    Track remote;
    remote.id = "77777777777777777777777777777777";
    remote.title = "Remote";
    const auto stream = source.openStream(remote, StreamIntent::Playback);
    REQUIRE(stream != nullptr);
    CHECK(source.lastError().empty());

    Track rated;
    rated.id = "item-1";
    source.setRating(rated, 4);
    CHECK(source.lastError().empty());

    REQUIRE(transport.requests().size() == 3);
    CHECK(transport.requests()[0].url ==
          kBaseUrl + "/Items?searchTerm=pink&recursive=true&includeItemTypes=Audio"
                     "&sortBy=SortName&sortOrder=Ascending&limit=50&fields="
                     "SortName,RunTimeTicks,Album,AlbumArtist,Artists,ProductionYear,"
                     "IndexNumber,ParentIndexNumber,Container,NormalizationGain");
    CHECK(transport.requests()[1].url ==
          kBaseUrl + "/Audio/77777777777777777777777777777777/stream?static=true");
    CHECK(transport.requests()[2].url ==
          kBaseUrl + "/Users/user-1/Items/item-1/UserData");

    source.signOut();
    CHECK_FALSE(source.isAuthenticated());
    CHECK(source.openStream(remote, StreamIntent::Playback) == nullptr);
    CHECK(source.lastError() == "Not signed in");
}
