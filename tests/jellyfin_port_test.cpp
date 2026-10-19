// SPDX-License-Identifier: MIT
// FE-5 — koalapaw Jellyfin port: unit + fixture-replay tests.
//
// Every gateway interaction is replayed offline through the same
// JellyfinClient code path used against a live server (docs/tickets/FE-5).

#include "servers/jellyfin/ArtworkCache.hpp"
#include "servers/jellyfin/AuthHeaders.hpp"
#include "servers/jellyfin/DeviceIdentity.hpp"
#include "servers/jellyfin/Discovery.hpp"
#include "servers/jellyfin/FixtureTransport.hpp"
#include "servers/jellyfin/JellyfinClient.hpp"
#include "servers/jellyfin/Sha256.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace koalaamp::servers::jellyfin;
using koalaamp::library::SourceId;
using koalaamp::library::Track;

constexpr const char* kFixtureDir = KOALAAMP_FIXTURE_DIR;
constexpr const char* kUnauthorizedDir = KOALAAMP_FIXTURE_UNAUTHORIZED_DIR;
constexpr const char* kServersSrc = KOALAAMP_SERVERS_SRC;

const std::string kBaseUrl = "http://jellyfin.local:8096";
const std::string kParentId = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
const std::string kMachineId = "machine-under-test";
const std::string kDeviceId = deriveDeviceId(kMachineId, "tester");

DeviceProfile testProfile() {
    DeviceProfile profile;
    profile.clientName = "KoalaAmp";
    profile.deviceName = "device-under-test";
    profile.clientVersion = "0.0.1";
    return profile;
}

const std::string* headerValue(const HttpRequest& request, const std::string& name) {
    for (const auto& [key, value] : request.headers) {
        if (key == name) {
            return &value;
        }
    }
    return nullptr;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("SHA-256 matches the FIPS 180-4 vectors") {
    REQUIRE(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // > 64 bytes exercises the two-block tail path.
    REQUIRE(sha256Hex(std::string(1000, 'a')) ==
            "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

TEST_CASE("deriveDeviceId is the ticket's hash(machineId, userKey), stable per user") {
    const std::string a = deriveDeviceId(kMachineId, "alice");
    REQUIRE(a.size() == 64);
    REQUIRE(a == deriveDeviceId(kMachineId, "alice")); // re-login reuses the id
    REQUIRE(a != deriveDeviceId(kMachineId, "bob"));   // distinct per user
    REQUIRE(a != deriveDeviceId("other-machine", "alice"));
    // The install id must never leak into the header (AuthInterceptorTest.kt:111).
    REQUIRE(a != kMachineId);
}

TEST_CASE("randomUuid mints a v4 UUID") {
    const std::string uuid = randomUuid();
    REQUIRE(uuid.size() == 36);
    REQUIRE(uuid[14] == '4');
    REQUIRE((uuid[19] == '8' || uuid[19] == '9' || uuid[19] == 'a' || uuid[19] == 'b'));
    REQUIRE(uuid != randomUuid());
}

TEST_CASE("mediaBrowser header uses exactly the koalapaw field order") {
    const DeviceProfile profile = testProfile();
    REQUIRE(buildAuthHeader(profile, "dev-1", "") ==
            "MediaBrowser Client=\"KoalaAmp\", Device=\"device-under-test\", "
            "DeviceId=\"dev-1\", Version=\"0.0.1\"");
    REQUIRE(buildAuthHeader(profile, "dev-1", "tok-9") ==
            "MediaBrowser Client=\"KoalaAmp\", Device=\"device-under-test\", "
            "DeviceId=\"dev-1\", Version=\"0.0.1\", Token=\"tok-9\"");

    const auto headers = baseHeaders(profile, "dev-1", "tok-9");
    REQUIRE(headers.size() == 2);
    REQUIRE(headers[0].first == "Authorization");
    REQUIRE(headers[1] == std::pair<std::string, std::string>{"Accept", "application/json"});
}

TEST_CASE("discovery reply parsing mirrors koalapaw's leniency") {
    const auto good = parseDiscoveryReply(
        R"({"Address":"http://192.168.1.10:8096/","Id":"abc","Name":"Living Room"})");
    REQUIRE(good.has_value());
    REQUIRE(good->address == "http://192.168.1.10:8096"); // trailing slash trimmed
    REQUIRE(good->id == "abc");
    REQUIRE(good->name == "Living Room");

    const auto fallback = parseDiscoveryReply(R"({"Address":"http://10.0.0.2:8096"})");
    REQUIRE(fallback.has_value());
    REQUIRE(fallback->id == "http://10.0.0.2:8096"); // id/name fall back to address
    REQUIRE(fallback->name == "http://10.0.0.2:8096");

    REQUIRE_FALSE(parseDiscoveryReply(R"({"Name":"no address"})").has_value());
    REQUIRE_FALSE(parseDiscoveryReply(R"({"Address":"   "})").has_value());
    REQUIRE_FALSE(parseDiscoveryReply("not json at all").has_value());
}

TEST_CASE("authenticate replays a recorded exchange and builds a token-less header") {
    FixtureTransport transport{kFixtureDir};
    JellyfinClient client(transport, testProfile());

    const AuthOutcome outcome = client.authenticate(kBaseUrl, "tester", "correct-horse", kDeviceId);
    const auto* success = std::get_if<AuthSuccess>(&outcome);
    REQUIRE(success != nullptr);
    REQUIRE(success->session.accessToken == "fixture-token-1");
    REQUIRE(success->session.userId == "11111111111111111111111111111111");
    REQUIRE(success->session.userName == "tester");

    REQUIRE(transport.requests().size() == 1);
    const HttpRequest& request = transport.requests().front();
    REQUIRE(request.method == "POST");
    REQUIRE(request.url == kBaseUrl + "/Users/AuthenticateByName");
    REQUIRE(contains(request.body, "\"Username\":\"tester\""));
    REQUIRE(contains(request.body, "\"Pw\":\"correct-horse\""));

    const std::string* auth = headerValue(request, "Authorization");
    REQUIRE(auth != nullptr);
    REQUIRE(*auth == "MediaBrowser Client=\"KoalaAmp\", Device=\"device-under-test\", DeviceId=\"" +
                         kDeviceId + "\", Version=\"0.0.1\"");
    REQUIRE_FALSE(contains(*auth, "Token=")); // login carries no token
}

TEST_CASE("re-login loop: authenticating twice with the same DeviceId succeeds") {
    FixtureTransport transport{kFixtureDir};
    JellyfinClient client(transport, testProfile());

    const AuthOutcome first = client.authenticate(kBaseUrl, "tester", "correct-horse", kDeviceId);
    const AuthOutcome second = client.authenticate(kBaseUrl, "tester", "correct-horse", kDeviceId);
    REQUIRE(std::holds_alternative<AuthSuccess>(first));
    REQUIRE(std::holds_alternative<AuthSuccess>(second));
    // Same derived id both times — the trap is a changed id invalidating the first token.
    REQUIRE(std::get<AuthSuccess>(first).session.accessToken == "fixture-token-1");
    REQUIRE(std::get<AuthSuccess>(second).session.accessToken == "fixture-token-2");
    REQUIRE(transport.requests().size() == 2);
    REQUIRE(headerValue(transport.requests()[0], "Authorization") != nullptr);
    REQUIRE(*headerValue(transport.requests()[0], "Authorization") ==
            *headerValue(transport.requests()[1], "Authorization"));
}

TEST_CASE("musicAlbumsWindow sends the koalapaw query and maps items") {
    FixtureTransport transport{kFixtureDir};
    JellyfinClient client(transport, testProfile());

    const ItemsOutcome outcome =
        client.musicAlbumsWindow(kBaseUrl, "fixture-token-1", kDeviceId, kParentId, 0, 50);
    const auto* success = std::get_if<ItemsSuccess>(&outcome);
    REQUIRE(success != nullptr);
    REQUIRE(success->window.totalRecordCount == 2);
    REQUIRE(success->window.items.size() == 2);

    const MediaItem& first = success->window.items[0];
    REQUIRE(first.name == "Blue Train");
    REQUIRE(first.type == "MusicAlbum");
    REQUIRE(first.sortName == "Blue Train");
    REQUIRE(first.albumArtist == "John Coltrane");
    REQUIRE(first.productionYear == 1957);
    REQUIRE(first.isFavorite);
    REQUIRE_FALSE(first.played);
    REQUIRE(success->window.items[1].name == "Kind of Blue");
    REQUIRE(success->window.items[1].played);

    REQUIRE(transport.requests().size() == 1);
    const HttpRequest& request = transport.requests().front();
    REQUIRE(request.method == "GET");
    REQUIRE(request.url ==
            kBaseUrl + "/Items?parentId=" + kParentId +
                "&recursive=true&includeItemTypes=MusicAlbum&sortBy=SortName"
                "&sortOrder=Ascending&startIndex=0&limit=50&fields=SortName&enableUserData=true");
    const std::string* auth = headerValue(request, "Authorization");
    REQUIRE(auth != nullptr);
    REQUIRE(contains(*auth, "Token=\"fixture-token-1\""));
    REQUIRE(contains(*auth, "DeviceId=\"" + kDeviceId + "\""));
}

TEST_CASE("musicArtistsWindow is atoZWindow with the artist kind") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    JellyfinClient client(transport, testProfile());

    const auto outcome =
        client.musicArtistsWindow(kBaseUrl, "fixture-token-1", kDeviceId, kParentId, 0, 50);
    const auto* success = std::get_if<ItemsSuccess>(&outcome);
    REQUIRE(success != nullptr);
    REQUIRE(success->window.items.size() == 2);
    CHECK(success->window.items[0].name == "John Coltrane");
    CHECK(success->window.items[0].type == "MusicArtist");
    CHECK(success->window.items[1].sortName == "Davis, Miles");

    // The ported query: one artist kind, SortName ascending, the same header discipline.
    REQUIRE(transport.requests().size() == 1);
    const HttpRequest& request = transport.requests().front();
    CHECK(contains(request.url, "includeItemTypes=MusicArtist"));
    CHECK(contains(request.url, "sortBy=SortName"));
    CHECK(contains(request.url, "enableUserData=true"));
    REQUIRE(headerValue(request, "Authorization") != nullptr);
    CHECK(contains(*headerValue(request, "Authorization"), "Token=\"fixture-token-1\""));
}

TEST_CASE("letterOffsets buckets the sorted listing and keeps the first index") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    JellyfinClient client(transport, testProfile());

    const auto outcome =
        client.letterOffsets(kBaseUrl, "fixture-token-1", kDeviceId, kParentId, "MusicArtist");
    const auto* success = std::get_if<OffsetsSuccess>(&outcome);
    REQUIRE(success != nullptr);
    // "50 Cent" opens "#", "Anderson .Paak" opens "A", "Beatles, The" opens "B"; the
    // later blank name and "Beach House" must not steal a bucket's first index.
    REQUIRE(success->offsets.size() == 3);
    CHECK(success->offsets.at("#") == 0);
    CHECK(success->offsets.at("A") == 1);
    CHECK(success->offsets.at("B") == 2);

    REQUIRE(transport.requests().size() == 1);
    CHECK(contains(transport.requests().front().url, "limit=100000"));
    CHECK(contains(transport.requests().front().url, "enableImages=false"));
}

TEST_CASE("search maps /Items audio results onto Track") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    JellyfinClient client(transport, testProfile());

    const auto outcome =
        client.search(kBaseUrl, "fixture-token-1", kDeviceId, kParentId, "pink", "Audio", 50);
    const auto* success = std::get_if<SearchSuccess>(&outcome);
    REQUIRE(success != nullptr);
    REQUIRE(success->tracks.size() == 3);

    const Track& first = success->tracks[0];
    CHECK(first.src == SourceId::Jellyfin);
    CHECK(first.id == "70000000000000000000000000000001");
    CHECK(first.title == "Pink Moon");
    CHECK(first.artist == "Nick Drake");
    CHECK(first.albumArtist == "Nick Drake");
    CHECK(first.album == "Pink Moon");
    CHECK(first.year == 1972);
    CHECK(first.trackNo == 1);
    CHECK(first.discNo == 1);
    CHECK(first.durationMs == 124700); // RunTimeTicks are 100 ns units.

    CHECK(success->tracks[2].artist == "Pink Floyd");
    CHECK(success->tracks[2].durationMs == 806200);

    REQUIRE(transport.requests().size() == 1);
    CHECK(contains(transport.requests().front().url, "searchTerm=pink"));
    CHECK(contains(transport.requests().front().url, "includeItemTypes=Audio"));
}

TEST_CASE("artwork caches by item+tag and honours max-age") {
    const auto dir = std::filesystem::temp_directory_path() / "koalaamp-artwork-test";
    std::filesystem::remove_all(dir);

    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    ArtworkCache cache(transport, testProfile(), dir);

    const std::string itemId = "22222222222222222222222222222222";
    const auto first =
        cache.primaryImage(kBaseUrl, "fixture-token-1", kDeviceId, itemId, "tag-blue");
    REQUIRE(first.has_value());
    CHECK(first->filename() == "22222222222222222222222222222222-tag-blue.png");
    {
        std::ifstream in(*first, std::ios::binary);
        REQUIRE(in.good());
        const std::string bytes{std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>()};
        CHECK(bytes == "fake-png-bytes");
    }
    REQUIRE(transport.requests().size() == 1);
    CHECK(contains(transport.requests().front().url, "/Images/Primary?tag=tag-blue"));
    REQUIRE(headerValue(transport.requests().front(), "Authorization") != nullptr);
    CHECK(contains(*headerValue(transport.requests().front(), "Authorization"),
                   "Token=\"fixture-token-1\""));

    // Within the recorded max-age the cache answers without a request; the fixture
    // transport has only one recording, so a second request would come back empty.
    const auto second =
        cache.primaryImage(kBaseUrl, "fixture-token-1", kDeviceId, itemId, "tag-blue");
    REQUIRE(second.has_value());
    CHECK(*second == *first);
    CHECK(transport.requests().size() == 1);

    CHECK(ArtworkCache::maxAgeSeconds("max-age=3600, public") == 3600);
    CHECK(ArtworkCache::maxAgeSeconds("max-age=60") == 60);
    CHECK(ArtworkCache::maxAgeSeconds("no-store") == -1);
    CHECK(ArtworkCache::maxAgeSeconds("no-cache, max-age=60") == -1);
    CHECK(ArtworkCache::maxAgeSeconds("") == -1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("an HTTP 401 maps to ItemsUnauthorized, not an exception") {
    FixtureTransport transport{kUnauthorizedDir};
    JellyfinClient client(transport, testProfile());

    const ItemsOutcome outcome =
        client.musicAlbumsWindow(kBaseUrl, "stale-token", kDeviceId, kParentId, 0, 50);
    REQUIRE(std::holds_alternative<ItemsUnauthorized>(outcome));
}

TEST_CASE("a transport failure maps to Unreachable") {
    // No fixture recorded -> transport reports a miss -> Unreachable.
    FixtureTransport transport{kUnauthorizedDir};
    JellyfinClient client(transport, testProfile());
    const AuthOutcome outcome = client.authenticate(kBaseUrl, "tester", "pw", kDeviceId);
    REQUIRE(std::holds_alternative<AuthUnreachable>(outcome));
}

TEST_CASE("no deprecated token transport appears anywhere in the spike source") {
    // AGENTS.md + spec §8.2: X-Emby-Token / api_key must never be reintroduced.
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(kServersSrc)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path().string());
        }
    }
    REQUIRE_FALSE(files.empty());
    for (const std::string& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        const std::string text = buffer.str();
        INFO(file);
        REQUIRE_FALSE(contains(text, "X-Emby-Token"));
        REQUIRE_FALSE(contains(text, "api_key"));
    }
}
