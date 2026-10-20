// SPDX-License-Identifier: MIT
// SV-3 — Plex auth, browse, source and GDM. Fixture-replayed, no network.

#include "servers/jellyfin/FixtureTransport.hpp"
#include "servers/plex/PlexAuth.hpp"
#include "servers/plex/PlexClient.hpp"
#include "servers/plex/PlexGdm.hpp"
#include "servers/plex/PlexSource.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace koalaamp::servers::plex;
using koalaamp::servers::jellyfin::FixtureTransport;

constexpr const char* kFixtureDir = KOALAAMP_PLEX_FIXTURE_DIR;
const std::string kPlexTv = "https://plex.tv";
const std::string kPms = "http://plex.local:32400";
const std::string kToken = "pms-token";

PlexClientInfo clientInfo() {
    return PlexClientInfo{"client-id", "KoalaAmp", "0.0.1", "tester", "Generic"};
}

const std::string* headerValue(const koalaamp::servers::jellyfin::HttpRequest& request,
                               const std::string& name) {
    for (const auto& [key, value] : request.headers) {
        if (key == name) {
            return &value;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("the PIN link flow resolves the account and its PMS resource") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    PlexAuth auth{transport, clientInfo()};

    const PlexPinOutcome pinOutcome = auth.createPin();
    const auto* pin = std::get_if<PlexPin>(&pinOutcome);
    REQUIRE(pin != nullptr);
    CHECK(pin->id == "12345");
    CHECK(pin->code == "ABCD");
    CHECK(PlexAuth::linkUrl() == "https://plex.tv/link");

    CHECK(std::holds_alternative<PlexLinkPending>(auth.pollPin(pin->id)));

    const PlexPollOutcome complete = auth.pollPin(pin->id);
    const auto* account = std::get_if<PlexAccount>(&complete);
    REQUIRE(account != nullptr);
    CHECK(account->token == "account-token");
    CHECK(account->userId == "user-uuid");
    CHECK(account->userName == "tester");

    const PlexResourcesOutcome resources = auth.resources(account->token);
    const auto* servers = std::get_if<std::vector<PlexResource>>(&resources);
    REQUIRE(servers != nullptr);
    REQUIRE(servers->size() == 1); // the client-only resource is dropped
    CHECK((*servers)[0].id == "server-1");
    CHECK((*servers)[0].accessToken == kToken);
    CHECK((*servers)[0].chooseUri() == "http://192.168.1.10:32400");

    REQUIRE(transport.requests().size() == 5);
    CHECK(transport.requests()[0].url == kPlexTv + "/api/v2/pins");
    CHECK(transport.requests()[1].url == kPlexTv + "/api/v2/pins/12345");
    CHECK(transport.requests()[3].url == kPlexTv + "/api/v2/user");
    CHECK(transport.requests()[4].url ==
          kPlexTv + "/api/v2/resources?includeHttps=1&includeRelay=1");
    REQUIRE(headerValue(transport.requests()[4], "X-Plex-Token") != nullptr);
    CHECK(*headerValue(transport.requests()[4], "X-Plex-Token") == "account-token");
    for (const auto& request : transport.requests()) {
        CHECK(request.url.find("account-token") == std::string::npos);
    }
}

TEST_CASE("the PMS client browses music sections, albums, tracks and search") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    PlexClient client{transport, clientInfo(), kPms, kToken};

    const auto identity = client.identity();
    REQUIRE(identity.has_value());
    CHECK(identity->id == "server-1");
    CHECK(identity->name == "Plex Box");
    CHECK(identity->version == "1.41.0");

    const auto sections = client.sections();
    REQUIRE(sections.has_value());
    REQUIRE(sections->size() == 2);
    CHECK((*sections)[0].id == "1");
    CHECK((*sections)[0].type == "artist");

    const auto albums = client.albums("1", 0, 50);
    REQUIRE(albums.has_value());
    REQUIRE(albums->size() == 1);
    CHECK((*albums)[0].id == "100");
    CHECK((*albums)[0].title == "Plex Album");
    CHECK((*albums)[0].artist == "Plex Artist");
    CHECK((*albums)[0].year == 2021);

    const auto tracks = client.albumTracks("100");
    REQUIRE(tracks.has_value());
    REQUIRE(tracks->size() == 2);
    CHECK((*tracks)[0].id == "200");
    CHECK((*tracks)[0].artist == "Plex Artist");
    CHECK((*tracks)[0].album == "Plex Album");
    CHECK((*tracks)[0].trackNo == 1);
    CHECK((*tracks)[0].durationMs == 180000);
    CHECK((*tracks)[0].codec == "flac");
    CHECK((*tracks)[0].bitrate == 900000);
    CHECK((*tracks)[0].path == "/library/parts/555/123/file.flac");

    const auto hits = client.search("pink", 20);
    REQUIRE(hits.has_value());
    REQUIRE(hits->size() == 1);
    CHECK((*hits)[0].title == "First Track");

    const auto art = client.artwork("/library/metadata/100/thumb/1", 0, 0);
    REQUIRE(art.has_value());
    CHECK(*art == "JPEG");

    CHECK(client.rate("200", 4));
    REQUIRE(transport.requests().size() == 7);
    CHECK(transport.requests()[6].url ==
          kPms + "/:/rate?key=200&identifier=com.plexapp.plugins.library&rating=8");
    CHECK(transport.requests()[6].method == "PUT");
    for (const auto& request : transport.requests()) {
        CHECK(request.url.find(kToken) == std::string::npos);
        if (request.url.find("/identity") == std::string::npos) {
            REQUIRE(headerValue(request, "X-Plex-Token") != nullptr);
            CHECK(*headerValue(request, "X-Plex-Token") == kToken);
        }
    }
}

TEST_CASE("the Plex source searches, streams with Range and rates") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    PlexSource source{transport, clientInfo(),
                      PlexSession{kPms, kToken, "server-1", "user-uuid", "tester"}};
    CHECK(source.isAuthenticated());

    koalaamp::library::Query query;
    query.text = "pink";
    query.limit = 20;
    koalaamp::servers::jellyfin::NoCancellation cancel;
    const auto found = source.search(query, cancel);
    REQUIRE(found.size() == 1);
    CHECK(found[0].path == "/library/parts/555/123/file.flac");

    const auto stream = source.openStream(found[0], koalaamp::library::StreamIntent::Playback);
    REQUIRE(stream != nullptr);
    char first[5] = {};
    stream->read(first, 4);
    CHECK(std::string(first, 4) == "0123");
    stream->seekg(20, std::ios::beg);
    REQUIRE(stream->good());
    char later[5] = {};
    stream->read(later, 4);
    CHECK(std::string(later, 4) == "abcd");

    koalaamp::library::Track rated;
    rated.id = "200";
    source.setRating(rated, 4);
    CHECK(source.lastError().empty());

    source.signOut();
    CHECK_FALSE(source.isAuthenticated());
    CHECK(source.openStream(found[0], koalaamp::library::StreamIntent::Playback) == nullptr);
    CHECK(source.lastError() == "Not signed in");
}

TEST_CASE("GDM replies map to a server address and reject incomplete ones") {
    const std::string reply =
        "HTTP/1.1 200 OK\r\nName: Plex Box\r\nPort: 32400\r\n"
        "Resource-Identifier: server-1\r\n\r\n";
    const auto server = parseGdmReply(reply, "192.168.1.10");
    REQUIRE(server.has_value());
    CHECK(server->id == "server-1");
    CHECK(server->name == "Plex Box");
    CHECK(server->address == "http://192.168.1.10:32400");

    CHECK_FALSE(parseGdmReply("HTTP/1.1 200 OK\r\nName: Plex Box\r\n\r\n", "10.0.0.2")
                    .has_value());
    CHECK(parseGdmReply("Name: X\r\nResource-Identifier: id\r\n\r\n", "10.0.0.3")->address ==
          "http://10.0.0.3:32400");
}
