// SPDX-License-Identifier: MIT
// SC-1 — client-side scrobbling, replayed fully offline.
//
// Covers: MD5 known answers, Last.fm api_sig/request shapes, auth/session and
// error parsing, batched scrobble bodies, ListenBrainz payload/header shapes,
// the listen-policy matrix, and the persisted retry queue.

#include "scrobble/CurlTransport.hpp"
#include "scrobble/LastFmClient.hpp"
#include "scrobble/ListenBrainzClient.hpp"
#include "scrobble/Md5.hpp"
#include "scrobble/PlaybackScrobbler.hpp"
#include "scrobble/ScrobbleCoordinator.hpp"
#include "scrobble/ScrobbleQueue.hpp"
#include "scrobble/ScrobbleService.hpp"
#include "scrobble/SecretStore.hpp"
#include "scrobble/Transport.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace koalaamp::scrobble;

constexpr const char* kApiKey = "api-key";
constexpr const char* kSharedSecret = "shared-secret";
constexpr const char* kSessionKey = "sk-123";
constexpr const char* kRequestToken = "request-token";

class FakeTransport final : public ITransport {
public:
    HttpResponse perform(const HttpRequest& request) override {
        requests.push_back(request);
        return next;
    }

    std::vector<HttpRequest> requests;
    HttpResponse next;
};

std::string urlDecode(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            out.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') {
                    return c - '0';
                }
                if (c >= 'A' && c <= 'F') {
                    return c - 'A' + 10;
                }
                return c - 'a' + 10;
            };
            out.push_back(static_cast<char>((hex(value[i + 1]) << 4) | hex(value[i + 2])));
            i += 2;
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::map<std::string, std::string> parseForm(const std::string& body) {
    std::map<std::string, std::string> fields;
    std::size_t start = 0;
    while (start <= body.size()) {
        const auto amp = body.find('&', start);
        const std::string part =
            body.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!part.empty()) {
            const auto equals = part.find('=');
            if (equals == std::string::npos) {
                fields[urlDecode(part)] = std::string{};
            } else {
                fields[urlDecode(std::string_view(part).substr(0, equals))] =
                    urlDecode(std::string_view(part).substr(equals + 1));
            }
        }
        if (amp == std::string::npos) {
            break;
        }
        start = amp + 1;
    }
    return fields;
}

const std::string* header(const HttpRequest& request, std::string_view name) {
    for (const auto& [key, value] : request.headers) {
        if (key == name) {
            return &value;
        }
    }
    return nullptr;
}

bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

ScrobbleTrack trackOne() {
    ScrobbleTrack track;
    track.artist = "Artist One";
    track.title = "Track One";
    track.album = "Album One";
    track.albumArtist = "Album Artist";
    track.durationMs = 215000;
    track.trackNumber = 3;
    return track;
}

ScrobbleTrack trackTwo() {
    ScrobbleTrack track;
    track.artist = "Artist Two";
    track.title = "Track Two";
    track.album = "Album Two";
    track.albumArtist = "Album Artist";
    track.durationMs = 180000;
    track.trackNumber = 1;
    track.recordingMbid = "recording-mbid-2";
    return track;
}

/// Unique path under the system temp directory, removed on scope exit.
class TempQueueFile {
public:
    TempQueueFile() {
        static int counter = 0;
        ++counter;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("koalaamp-scrobble-" + std::to_string(counter) + "-" + std::to_string(stamp) +
                 ".json");
    }
    ~TempQueueFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::path temporary = path_;
        temporary += ".tmp";
        std::filesystem::remove(temporary, ignored);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

struct FakeLastFm final : ILastFmSubmitter {
    bool updateNowPlaying(const ScrobbleTrack& track) override {
        ++nowPlayingCount;
        lastNowPlaying = track;
        return true;
    }
    bool scrobble(const std::vector<TimedScrobble>& batch) override {
        if (fail) {
            return false;
        }
        batches.push_back(batch);
        return true;
    }

    int nowPlayingCount{0};
    ScrobbleTrack lastNowPlaying;
    std::vector<std::vector<TimedScrobble>> batches;
    bool fail{false};
};

struct FakeListenBrainz final : IListenBrainzSubmitter {
    bool submitPlayingNow(const ScrobbleTrack&) override {
        ++playingNowCount;
        return true;
    }
    bool submitSingle(const ScrobbleTrack& track, std::int64_t listenedAt) override {
        if (fail) {
            return false;
        }
        singles.push_back(TimedScrobble{track, listenedAt});
        return true;
    }

    int playingNowCount{0};
    std::vector<TimedScrobble> singles;
    bool fail{false};
};

} // namespace

TEST_CASE("MD5 matches the RFC 1321 test vectors") {
    REQUIRE(md5Hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    REQUIRE(md5Hex("a") == "0cc175b9c0f1b6a831c399e269772661");
    REQUIRE(md5Hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    REQUIRE(md5Hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    REQUIRE(md5Hex("abcdefghijklmnopqrstuvwxyz") == "c3fcd3d76192e4007dfb496cca67e13b");
    REQUIRE(md5Hex("The quick brown fox jumps over the lazy dog") ==
            "9e107d9d372bb6826bd81d3542a419d6");
    // One million 'a' exercises the multi-block path; 1000 bytes the 2-block tail.
    REQUIRE(md5Hex(std::string(1000, 'a')) == "cabe45dcc9ae5b66ba86600cca6b8ba8");
    REQUIRE(md5Hex(std::string(1000000, 'a')) == "7707d6ae4e027c70eea2a935c2296f21");
    // The Last.fm authentication spec's own example string.
    REQUIRE(md5Hex("api_keyxxxxxxxxxxmethodauth.getSessiontokenyyyyyyilovecher") ==
            "b87d61da3cda91a8b6746c4aef55d6f8");
}

TEST_CASE("Last.fm auth.getToken signs sorted params and keeps the secret out of the URL") {
    FakeTransport transport;
    transport.next = HttpResponse{200, R"({"token":"request-token"})", {}, {}};
    LastFmClient client({kApiKey, kSharedSecret}, transport);

    const auto token = client.getToken();
    REQUIRE(token.has_value());
    REQUIRE(*token == kRequestToken);
    REQUIRE(client.lastError().ok());
    REQUIRE(transport.requests.size() == 1);

    const HttpRequest& request = transport.requests.front();
    REQUIRE(request.method == "POST");
    REQUIRE(request.url == "https://ws.audioscrobbler.com/2.0/");
    REQUIRE(!contains(request.url, "?"));
    REQUIRE(!contains(request.url, kSharedSecret));
    REQUIRE(!contains(request.url, kApiKey));
    REQUIRE(header(request, "Content-Type") != nullptr);
    REQUIRE(*header(request, "Content-Type") == "application/x-www-form-urlencoded");

    const auto fields = parseForm(request.body);
    REQUIRE(fields.at("method") == "auth.getToken");
    REQUIRE(fields.at("api_key") == kApiKey);
    REQUIRE(fields.at("format") == "json");
    // md5("api_keyapi-keymethodauth.getTokenshared-secret")
    REQUIRE(fields.at("api_sig") == "ce05f5b24c2b9c91934cdacb782a6677");
    REQUIRE(!contains(request.body, kSharedSecret));
}

TEST_CASE("Last.fm desktop auth URL carries the key and token only") {
    REQUIRE(lastFmAuthUrl(kApiKey, kRequestToken) ==
            "https://www.last.fm/api/auth/?api_key=api-key&token=request-token");
    REQUIRE(lastFmAuthUrl("a b", "t/k") ==
            "https://www.last.fm/api/auth/?api_key=a%20b&token=t%2Fk");
}

TEST_CASE("Last.fm auth.getSession parses the session and signs the token") {
    FakeTransport transport;
    transport.next = HttpResponse{
        200, R"({"session":{"name":"tester","key":"session-key","subscriber":1}})", {}, {}};
    LastFmClient client({kApiKey, kSharedSecret}, transport);

    const auto session = client.getSession(kRequestToken);
    REQUIRE(session.has_value());
    REQUIRE(session->name == "tester");
    REQUIRE(session->key == "session-key");
    REQUIRE(session->subscriber);

    const auto fields = parseForm(transport.requests.front().body);
    REQUIRE(fields.at("method") == "auth.getSession");
    REQUIRE(fields.at("token") == kRequestToken);
    // md5("api_keyapi-keymethodauth.getSessiontokenrequest-tokenshared-secret")
    REQUIRE(fields.at("api_sig") == "3c92ec0ff31c1ca1f3df0ee3cdc1406a");
    REQUIRE(!contains(transport.requests.front().url, "session-key"));
}

TEST_CASE("Last.fm error bodies map to LastFmError") {
    FakeTransport transport;
    LastFmClient client({kApiKey, kSharedSecret}, transport);

    transport.next = HttpResponse{
        400,
        R"({"error":6,"message":"Invalid parameters - Your request is missing a required parameter"})",
        {},
        {}};
    REQUIRE(!client.getSession(kRequestToken).has_value());
    REQUIRE(client.lastError().code == 6);
    REQUIRE(client.lastError().httpStatus == 400);
    REQUIRE(contains(client.lastError().message, "Invalid parameters"));
    REQUIRE(!client.lastError().ok());

    client.setSessionKey(kSessionKey);
    transport.next = HttpResponse{
        403, R"({"error":9,"message":"Invalid session key - Please re-authenticate"})", {}, {}};
    REQUIRE(!client.updateNowPlaying(trackOne()));
    REQUIRE(client.lastError().code == 9);

    // A transport failure never produces an HTTP error code.
    transport.next = HttpResponse{};
    transport.next.error = "connection refused";
    REQUIRE(!client.getToken().has_value());
    REQUIRE(client.lastError().transport);
    REQUIRE(client.lastError().message == "connection refused");
}

TEST_CASE("Last.fm updateNowPlaying sends the track body without a timestamp") {
    FakeTransport transport;
    transport.next = HttpResponse{200, R"({"nowplaying":{"track":{"#text":"Track One"}}})", {}, {}};
    LastFmClient client({kApiKey, kSharedSecret}, transport);
    client.setSessionKey(kSessionKey);

    REQUIRE(client.updateNowPlaying(trackOne()));
    REQUIRE(transport.requests.size() == 1);
    const HttpRequest& request = transport.requests.front();
    REQUIRE(request.url == "https://ws.audioscrobbler.com/2.0/");
    REQUIRE(!contains(request.url, kSessionKey));

    const auto fields = parseForm(request.body);
    REQUIRE(fields.at("method") == "track.updateNowPlaying");
    REQUIRE(fields.at("sk") == kSessionKey);
    REQUIRE(fields.at("artist") == "Artist One");
    REQUIRE(fields.at("track") == "Track One");
    REQUIRE(fields.at("album") == "Album One");
    REQUIRE(fields.at("albumArtist") == "Album Artist");
    REQUIRE(fields.at("duration") == "215");
    REQUIRE(fields.at("trackNumber") == "3");
    REQUIRE(fields.find("timestamp") == fields.end());
    // md5 over album/albumArtist/api_key/artist/duration/method/sk/track/trackNumber.
    REQUIRE(fields.at("api_sig") == "6ce38eeb4b00a4c5502a334384846f75");
}

TEST_CASE("Last.fm track.scrobble batches with array notation and parses accepted counts") {
    FakeTransport transport;
    transport.next = HttpResponse{
        200,
        R"({"scrobbles":{"scrobble":[{"track":{"#text":"Track One"}},{"track":{"#text":"Track Two"}}],"@attr":{"accepted":"2","ignored":"0"}}})",
        {},
        {}};
    LastFmClient client({kApiKey, kSharedSecret}, transport);
    client.setSessionKey(kSessionKey);

    std::vector<TimedScrobble> batch = {
        TimedScrobble{trackOne(), 1700000000},
        TimedScrobble{trackTwo(), 1700000100},
    };
    REQUIRE(client.scrobble(batch));
    REQUIRE(client.lastError().ok());
    REQUIRE(client.lastScrobbleResult().accepted == 2);
    REQUIRE(client.lastScrobbleResult().ignored == 0);

    const HttpRequest& request = transport.requests.front();
    REQUIRE(request.method == "POST");
    REQUIRE(!contains(request.url, kSessionKey));
    REQUIRE(!contains(request.url, "?"));
    const auto fields = parseForm(request.body);

    REQUIRE(fields.at("method") == "track.scrobble");
    REQUIRE(fields.at("sk") == kSessionKey);
    REQUIRE(fields.at("artist[0]") == "Artist One");
    REQUIRE(fields.at("track[0]") == "Track One");
    REQUIRE(fields.at("album[0]") == "Album One");
    REQUIRE(fields.at("albumArtist[0]") == "Album Artist");
    REQUIRE(fields.at("duration[0]") == "215");
    REQUIRE(fields.at("trackNumber[0]") == "3");
    REQUIRE(fields.at("timestamp[0]") == "1700000000");
    REQUIRE(fields.at("artist[1]") == "Artist Two");
    REQUIRE(fields.at("track[1]") == "Track Two");
    REQUIRE(fields.at("mbid[1]") == "recording-mbid-2");
    REQUIRE(fields.at("timestamp[1]") == "1700000100");
    REQUIRE(fields.find("artist[2]") == fields.end());
    REQUIRE(fields.find("timestamp[2]") == fields.end());
    // Pinned against an independent implementation; the signature sorts the
    // indexed parameter names ASCII-wise ("albumArtist[0]" < "album[0]").
    REQUIRE(fields.at("api_sig") == "378d06b860d18becb984386d9a55250c");
    REQUIRE(!contains(request.body, kSharedSecret));
}

TEST_CASE("Last.fm rejects over-long batches and missing credentials locally") {
    FakeTransport transport;
    LastFmClient client({kApiKey, kSharedSecret}, transport);
    std::vector<TimedScrobble> tooMany(51, TimedScrobble{trackOne(), 1700000000});
    REQUIRE(!client.scrobble(tooMany));
    REQUIRE(client.lastError().message.find("50") != std::string::npos);
    REQUIRE(transport.requests.empty());

    REQUIRE(!client.updateNowPlaying(trackOne()));
    REQUIRE(client.lastError().message.find("session") != std::string::npos);
    REQUIRE(transport.requests.empty());
}

TEST_CASE("ListenBrainz playing_now carries token header and MBIDs, never a URL token") {
    FakeTransport transport;
    transport.next = HttpResponse{200, R"({"status":"ok","code":200})", {}, {}};
    ListenBrainzClient client("lb-token", transport);

    ScrobbleTrack track;
    track.artist = "Rick Astley";
    track.title = "Never Gonna Give You Up";
    track.album = "Whenever you need somebody";
    track.recordingMbid = "98255a8c-017a-4bc7-8dd6-1fa36124572b";
    track.releaseMbid = "bf9e91ea-8029-4a04-a26a-224e00a83266";
    track.artistMbids = {"db92a151-1ac2-438b-bc43-b82e149ddd50"};

    REQUIRE(client.submitPlayingNow(track));
    REQUIRE(client.lastError().ok());
    REQUIRE(transport.requests.size() == 1);

    const HttpRequest& request = transport.requests.front();
    REQUIRE(request.method == "POST");
    REQUIRE(request.url == "https://api.listenbrainz.org/1/submit-listens");
    REQUIRE(request.url.find("lb-token") == std::string::npos);
    REQUIRE(request.body.find("lb-token") == std::string::npos);
    REQUIRE(*header(request, "Authorization") == "Token lb-token");
    REQUIRE(*header(request, "Content-Type") == "application/json");

    const auto body = nlohmann::json::parse(request.body);
    REQUIRE(body.at("listen_type") == "playing_now");
    REQUIRE(body.at("payload").size() == 1);
    const auto& listen = body.at("payload").at(0);
    REQUIRE(listen.find("listened_at") == listen.end());
    const auto& metadata = listen.at("track_metadata");
    REQUIRE(metadata.at("artist_name") == "Rick Astley");
    REQUIRE(metadata.at("track_name") == "Never Gonna Give You Up");
    REQUIRE(metadata.at("release_name") == "Whenever you need somebody");
    const auto& additional = metadata.at("additional_info");
    REQUIRE(additional.at("recording_mbid") == "98255a8c-017a-4bc7-8dd6-1fa36124572b");
    REQUIRE(additional.at("release_mbid") == "bf9e91ea-8029-4a04-a26a-224e00a83266");
    REQUIRE(additional.at("artist_mbids") ==
            nlohmann::json::array({"db92a151-1ac2-438b-bc43-b82e149ddd50"}));
    REQUIRE(additional.at("submission_client") == "KoalaAmp");
}

TEST_CASE("ListenBrainz single carries listened_at and omits absent metadata") {
    FakeTransport transport;
    transport.next = HttpResponse{200, R"({"status":"ok","code":200})", {}, {}};
    ListenBrainzClient client("lb-token", transport);

    ScrobbleTrack track;
    track.artist = "Artist One";
    track.title = "Track One";

    REQUIRE(client.submitSingle(track, 1443521965));
    const auto body = nlohmann::json::parse(transport.requests.front().body);
    REQUIRE(body.at("listen_type") == "single");
    const auto& listen = body.at("payload").at(0);
    REQUIRE(listen.at("listened_at") == 1443521965);
    const auto& metadata = listen.at("track_metadata");
    REQUIRE(metadata.find("release_name") == metadata.end());
    const auto& additional = metadata.at("additional_info");
    REQUIRE(additional.find("recording_mbid") == additional.end());
    REQUIRE(additional.find("release_mbid") == additional.end());
    REQUIRE(additional.find("artist_mbids") == additional.end());
    REQUIRE(additional.at("submission_client") == "KoalaAmp");
}

TEST_CASE("ListenBrainz maps HTTP failures and refuses a missing token") {
    FakeTransport transport;
    ListenBrainzClient client("lb-token", transport);
    transport.next =
        HttpResponse{401, R"({"code":401,"error":"Invalid authorization token."})", {}, {}};
    REQUIRE(!client.submitSingle(trackOne(), 1700000000));
    REQUIRE(client.lastError().httpStatus == 401);
    REQUIRE(client.lastError().message == "Invalid authorization token.");
    REQUIRE(!client.lastError().ok());

    FakeTransport unused;
    ListenBrainzClient anonymous(std::string{}, unused);
    REQUIRE(!anonymous.submitPlayingNow(trackOne()));
    REQUIRE(unused.requests.empty());
}

TEST_CASE("policy: the threshold is half the track, capped at four minutes") {
    REQUIRE(ScrobbleCoordinator::thresholdMs(0) == 4 * 60 * 1000);
    REQUIRE(ScrobbleCoordinator::thresholdMs(300000) == 150000);
    REQUIRE(ScrobbleCoordinator::thresholdMs(600000) == 4 * 60 * 1000);
    REQUIRE(ScrobbleCoordinator::thresholdMs(30000) == 15000);
}

TEST_CASE("policy: below threshold stays silent, at threshold exactly one scrobble") {
    FakeLastFm lastFm;
    ScrobbleCoordinator coordinator;
    coordinator.setMode(ScrobbleMode::Client);
    coordinator.setLastFm(&lastFm);

    coordinator.onTrackStarted(trackOne(), 1700000000);
    REQUIRE(lastFm.nowPlayingCount == 1);
    REQUIRE(lastFm.batches.empty());
    REQUIRE(!coordinator.currentPlayScrobbled());

    coordinator.onProgress(149999, 300000);
    REQUIRE(lastFm.batches.empty());

    coordinator.onProgress(150000, 300000);
    REQUIRE(lastFm.batches.size() == 1);
    REQUIRE(lastFm.batches.front().size() == 1);
    REQUIRE(lastFm.batches.front().front().timestampUnix == 1700000000);
    REQUIRE(coordinator.currentPlayScrobbled());

    coordinator.onProgress(150001, 300000);
    coordinator.onProgress(300000, 300000);
    coordinator.onTrackEnded();
    REQUIRE(lastFm.batches.size() == 1);
}

TEST_CASE("policy: four-minute override for long and unknown-duration tracks") {
    FakeLastFm lastFm;
    ScrobbleCoordinator coordinator;
    coordinator.setMode(ScrobbleMode::Client);
    coordinator.setLastFm(&lastFm);

    ScrobbleTrack longTrack = trackOne();
    longTrack.durationMs = 600000; // 50% would be 5 minutes
    coordinator.onTrackStarted(longTrack, 1700000000);
    coordinator.onProgress(239999, 600000);
    REQUIRE(lastFm.batches.empty());
    coordinator.onProgress(240000, 600000);
    REQUIRE(lastFm.batches.size() == 1);

    FakeLastFm unknownLastFm;
    ScrobbleCoordinator unknown;
    unknown.setMode(ScrobbleMode::Client);
    unknown.setLastFm(&unknownLastFm);
    ScrobbleTrack unknownLength = trackOne();
    unknownLength.durationMs = 0;
    unknown.onTrackStarted(unknownLength, 1700000001);
    unknown.onProgress(239999, 0);
    REQUIRE(unknownLastFm.batches.empty());
    unknown.onProgress(240000, 0);
    REQUIRE(unknownLastFm.batches.size() == 1);
}

TEST_CASE("policy: seek and restart never scrobble the same play twice") {
    FakeLastFm lastFm;
    ScrobbleCoordinator coordinator;
    coordinator.setMode(ScrobbleMode::Client);
    coordinator.setLastFm(&lastFm);

    coordinator.onTrackStarted(trackOne(), 1700000000);
    coordinator.onProgress(150000, 300000);
    REQUIRE(lastFm.batches.size() == 1);

    coordinator.onProgress(0, 300000);      // seek back to the start
    coordinator.onProgress(150000, 300000); // and past the threshold again
    coordinator.onProgress(300000, 300000);
    REQUIRE(lastFm.batches.size() == 1);

    coordinator.onTrackStarted(trackOne(), 1700000000); // restart within the play
    coordinator.onProgress(300000, 300000);
    coordinator.onTrackEnded();
    REQUIRE(lastFm.batches.size() == 1);
    REQUIRE(lastFm.nowPlayingCount == 1);
}

TEST_CASE("policy: natural end scrobbles once, then a replay is a new listen") {
    FakeLastFm lastFm;
    ScrobbleCoordinator coordinator;
    coordinator.setMode(ScrobbleMode::Client);
    coordinator.setLastFm(&lastFm);

    coordinator.onTrackStarted(trackOne(), 1700000000);
    coordinator.onProgress(100000, 300000); // last tick short of the threshold
    coordinator.onTrackEnded();
    REQUIRE(lastFm.batches.size() == 1);

    coordinator.onTrackStarted(trackOne(), 1700000100);
    REQUIRE(lastFm.nowPlayingCount == 2);
    coordinator.onProgress(150000, 300000);
    REQUIRE(lastFm.batches.size() == 2);
    coordinator.onTrackEnded();
    REQUIRE(lastFm.batches.size() == 2);
}

TEST_CASE("policy: stop/skip below the threshold never scrobbles") {
    FakeLastFm lastFm;
    ScrobbleCoordinator coordinator;
    coordinator.setMode(ScrobbleMode::Client);
    coordinator.setLastFm(&lastFm);

    coordinator.onTrackStarted(trackOne(), 1700000000);
    coordinator.onProgress(149999, 300000);
    coordinator.onStopped();
    coordinator.onProgress(300000, 300000);
    coordinator.onTrackEnded();
    REQUIRE(lastFm.batches.empty());

    // The next start of the same track is a new play.
    coordinator.onTrackStarted(trackOne(), 1700000100);
    coordinator.onProgress(150000, 300000);
    REQUIRE(lastFm.batches.size() == 1);
}

TEST_CASE("policy: Off and Relay suppress now-playing and submission") {
    for (const ScrobbleMode mode : {ScrobbleMode::Off, ScrobbleMode::Relay}) {
        FakeLastFm lastFm;
        FakeListenBrainz listenBrainz;
        TempQueueFile queueFile;
        ScrobbleQueue queue(queueFile.path());
        REQUIRE(queue.load());
        ScrobbleCoordinator coordinator;
        coordinator.setMode(mode);
        coordinator.setLastFm(&lastFm);
        coordinator.setListenBrainz(&listenBrainz);
        coordinator.setQueue(&queue);

        coordinator.onTrackStarted(trackOne(), 1700000000);
        coordinator.onProgress(300000, 300000);
        coordinator.onTrackEnded();
        coordinator.onTrackStarted(trackTwo(), 1700000100);
        coordinator.onProgress(1000000, 1000000);

        REQUIRE(lastFm.nowPlayingCount == 0);
        REQUIRE(lastFm.batches.empty());
        REQUIRE(listenBrainz.playingNowCount == 0);
        REQUIRE(listenBrainz.singles.empty());
        REQUIRE(queue.size() == 0);
        REQUIRE(coordinator.pendingCount() == 0);
    }

    ScrobbleCoordinator fresh;
    REQUIRE(fresh.mode() == ScrobbleMode::Off);
}

TEST_CASE("policy: a failed submission is queued once and flushed on retry") {
    TempQueueFile queueFile;
    ScrobbleQueue queue(queueFile.path());
    REQUIRE(queue.load());

    FakeLastFm lastFm;
    lastFm.fail = true;
    ScrobbleCoordinator coordinator;
    coordinator.setMode(ScrobbleMode::Client);
    coordinator.setLastFm(&lastFm);
    coordinator.setQueue(&queue);

    coordinator.onTrackStarted(trackOne(), 1700000000);
    coordinator.onProgress(150000, 300000);
    REQUIRE(queue.size() == 1);
    REQUIRE(coordinator.pendingCount() == 1);
    REQUIRE(queue.entries().front().service == ListenService::LastFm);
    REQUIRE(queue.entries().front().timestampUnix == 1700000000);
    REQUIRE(queue.entries().front().attempts == 0);

    coordinator.onProgress(200000, 300000); // exactly-once: no second queue entry
    REQUIRE(queue.size() == 1);

    // A reopened queue sees the pending listen (process restart).
    ScrobbleQueue reopened(queueFile.path());
    REQUIRE(reopened.load());
    REQUIRE(reopened.size() == 1);

    lastFm.fail = false;
    coordinator.setQueue(&reopened);
    REQUIRE(coordinator.flushPending() == 1);
    REQUIRE(reopened.size() == 0);
    REQUIRE(lastFm.batches.size() == 1);
    REQUIRE(lastFm.batches.front().front().track.title == "Track One");

    // Flushing again is a no-op.
    REQUIRE(coordinator.flushPending() == 0);
    REQUIRE(lastFm.batches.size() == 1);
}

TEST_CASE("policy: ListenBrainz failures use the same queue") {
    TempQueueFile queueFile;
    ScrobbleQueue queue(queueFile.path());
    REQUIRE(queue.load());

    FakeListenBrainz listenBrainz;
    listenBrainz.fail = true;
    ScrobbleCoordinator coordinator;
    coordinator.setMode(ScrobbleMode::Client);
    coordinator.setListenBrainz(&listenBrainz);
    coordinator.setQueue(&queue);

    coordinator.onTrackStarted(trackOne(), 1700000000);
    coordinator.onProgress(150000, 300000);
    REQUIRE(queue.size() == 1);
    REQUIRE(queue.entries().front().service == ListenService::ListenBrainz);

    listenBrainz.fail = false;
    REQUIRE(coordinator.flushPending() == 1);
    REQUIRE(queue.size() == 0);
    REQUIRE(listenBrainz.singles.size() == 1);
    REQUIRE(listenBrainz.singles.front().timestampUnix == 1700000000);
}

TEST_CASE("queue persists atomically, dedups and flushes") {
    TempQueueFile file;
    ScrobbleQueue queue(file.path());
    REQUIRE(queue.load());
    REQUIRE(queue.size() == 0);

    PendingScrobble pending;
    pending.service = ListenService::LastFm;
    pending.track = trackOne();
    pending.timestampUnix = 1700000000;
    REQUIRE(queue.enqueue(pending));
    REQUIRE(!queue.enqueue(pending)); // same dedup key
    REQUIRE(queue.size() == 1);
    REQUIRE(queue.contains(listenDedupKey(ListenService::LastFm, trackOne(), 1700000000)));
    REQUIRE(queue.save());

    {
        std::ifstream in(file.path(), std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        const std::string contents = text.str();
        REQUIRE(contents.find(kSharedSecret) == std::string::npos);
        REQUIRE(contents.find(kSessionKey) == std::string::npos);
        REQUIRE(contents.find("lb-token") == std::string::npos);
        REQUIRE(contents.find("api_sig") == std::string::npos);
    }

    ScrobbleQueue reopened(file.path());
    REQUIRE(reopened.load());
    REQUIRE(reopened.size() == 1);
    REQUIRE(reopened.entries().front().dedupKey ==
            listenDedupKey(ListenService::LastFm, trackOne(), 1700000000));
    REQUIRE(reopened.entries().front().track.title == "Track One");
    REQUIRE(reopened.entries().front().timestampUnix == 1700000000);

    std::vector<PendingScrobble> sent;
    REQUIRE(reopened.flush([&](const std::vector<PendingScrobble>& batch) {
        sent = batch;
        return true;
    }) == 1);
    REQUIRE(sent.size() == 1);
    REQUIRE(reopened.size() == 0);

    ScrobbleQueue afterFlush(file.path());
    REQUIRE(afterFlush.load());
    REQUIRE(afterFlush.size() == 0);
}

TEST_CASE("queue keeps entries and counts retries when a flush fails") {
    TempQueueFile file;
    ScrobbleQueue queue(file.path());
    REQUIRE(queue.load());

    PendingScrobble pending;
    pending.service = ListenService::ListenBrainz;
    pending.track = trackTwo();
    pending.timestampUnix = 1700000100;
    REQUIRE(queue.enqueue(pending));
    REQUIRE(queue.save());

    REQUIRE(queue.flush([](const std::vector<PendingScrobble>&) { return false; }) == 0);
    REQUIRE(queue.size() == 1);
    REQUIRE(queue.entries().front().attempts == 1);

    ScrobbleQueue reopened(file.path());
    REQUIRE(reopened.load());
    REQUIRE(reopened.entries().front().attempts == 1);
    REQUIRE(reopened.entries().front().service == ListenService::ListenBrainz);
    REQUIRE(reopened.entries().front().track.recordingMbid == "recording-mbid-2");

    // A later successful flush removes it.
    REQUIRE(reopened.flush([](const std::vector<PendingScrobble>&) { return true; }) == 1);
    REQUIRE(reopened.size() == 0);
}

TEST_CASE("queue tolerates malformed files and skips malformed entries") {
    TempQueueFile file;
    {
        std::ofstream out(file.path(), std::ios::binary | std::ios::trunc);
        out << "{ this is not json";
    }
    ScrobbleQueue broken(file.path());
    REQUIRE(!broken.load());
    REQUIRE(broken.size() == 0);

    {
        std::ofstream out(file.path(), std::ios::binary | std::ios::trunc);
        out << R"({"version":1,"pending":[)"
               R"({"service":"lastfm","dedupKey":"keep","timestamp":1,"track":{"artist":"A","title":"T"}},)"
               R"({"service":"nope","track":{"artist":"A","title":"T"}},)"
               R"({"service":"lastfm","track":{"title":"missing artist"}},)"
               R"("not an object")"
               R"(]})";
    }
    ScrobbleQueue partial(file.path());
    REQUIRE(partial.load());
    REQUIRE(partial.size() == 1);
    REQUIRE(partial.entries().front().dedupKey == "keep");
    REQUIRE(partial.entries().front().timestampUnix == 1);

    {
        std::ofstream out(file.path(), std::ios::binary | std::ios::trunc);
        out << R"({"unexpected":true})";
    }
    ScrobbleQueue wrongRoot(file.path());
    REQUIRE(!wrongRoot.load());
    REQUIRE(wrongRoot.size() == 0);
}

TEST_CASE("secret store round-trips credentials under scrobble slots") {
    MemorySecretStore store;
    REQUIRE(loadLastFmSessionKey(store).empty());
    REQUIRE(rememberLastFmSession(store, "tester", "session-key"));
    REQUIRE(loadLastFmSessionKey(store) == "session-key");
    REQUIRE(store.load(kLastFmUsernameSlot).value() == "tester");

    REQUIRE(rememberListenBrainzToken(store, "lb-token"));
    REQUIRE(loadListenBrainzToken(store) == "lb-token");

    forgetLastFmSession(store);
    REQUIRE(loadLastFmSessionKey(store).empty());
    forgetListenBrainzToken(store);
    REQUIRE(loadListenBrainzToken(store).empty());
}

namespace {

class ThreadedFakeTransport final : public ITransport {
public:
    HttpResponse perform(const HttpRequest& request) override {
        std::lock_guard<std::mutex> lock(mutex_);
        requests.push_back(request);
        const bool nowPlaying = request.body.find("track.updateNowPlaying") != std::string::npos;
        if (nowPlaying) {
            return HttpResponse{200, R"({"nowplaying":{"track":{"#text":"ok"}}})", {}, {}};
        }
        return HttpResponse{
            200, R"({"scrobbles":{"@attr":{"accepted":"1","ignored":"0"}}})", {}, {}};
    }

    std::size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests.size();
    }
    bool saw(const std::string& needle) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& request : requests) {
            if (request.body.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

private:
    mutable std::mutex mutex_;
    std::vector<HttpRequest> requests;
};

} // namespace

TEST_CASE("ScrobbleService runs now-playing and submission on its worker") {
    TempQueueFile file;
    ThreadedFakeTransport transport;
    {
        ScrobbleService service{transport,
                                file.path(),
                                ScrobbleMode::Client,
                                LastFmCredentials{kApiKey, kSharedSecret},
                                kSessionKey,
                                ""};
        service.onTrackStarted(trackOne(), 1000);
        service.onProgress(120000, 215000);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (transport.count() < 2 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(transport.saw("track.updateNowPlaying"));
        CHECK(transport.saw("track.scrobble"));
        CHECK(service.pendingCount() == 0);
        service.onTrackEnded();
    }
    // Destructor joins before the queue file is removed.
}

TEST_CASE("library track metadata carries MBIDs into the scrobble shape") {
    koalaamp::library::Track track;
    track.artist = "Artist";
    track.title = "Title";
    track.album = "Album";
    track.albumArtist = "Album Artist";
    track.durationMs = 123000;
    track.trackNo = 4;
    track.recordingMbid = "recording-id";
    track.releaseMbid = "release-id";
    track.artistMbids = {"artist-a", "artist-b"};

    const ScrobbleTrack scrobble = scrobbleTrackFrom(track);
    CHECK(scrobble.artist == "Artist");
    CHECK(scrobble.title == "Title");
    CHECK(scrobble.durationMs == 123000);
    CHECK(scrobble.trackNumber == 4);
    CHECK(scrobble.recordingMbid == "recording-id");
    CHECK(scrobble.releaseMbid == "release-id");
    CHECK(scrobble.artistMbids == std::vector<std::string>{"artist-a", "artist-b"});
    CHECK(scrobble.valid());
}
