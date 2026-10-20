// SPDX-License-Identifier: MIT
// SV-2 — `/socket` remote control: auth placement, message catalog, keepalive,
// routing and reconnect policy. Everything runs through a scripted transport;
// no server is needed.

#include "servers/jellyfin/JellyfinRemote.hpp"
#include "servers/jellyfin/WebSocket.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace koalaamp::servers::jellyfin;

DeviceProfile testProfile() {
    DeviceProfile profile;
    profile.clientName = "KoalaAmp";
    profile.deviceName = "device-under-test";
    profile.clientVersion = "0.0.1";
    return profile;
}

class ScriptedTransport final : public IWebSocketTransport {
public:
    std::vector<std::string> incoming;
    std::vector<std::string> sent;
    bool connectCalled{false};
    bool closeCalled{false};

    bool connect(std::string*) override {
        connectCalled = true;
        return true;
    }

    bool sendText(std::string_view text, std::string*) override {
        sent.emplace_back(text);
        return true;
    }

    bool receive(std::string* text, int, bool* closed, std::string*) override {
        if (incoming.empty()) {
            *closed = true;
            return false;
        }
        *text = std::move(incoming.front());
        incoming.erase(incoming.begin());
        *closed = false;
        return true;
    }

    void close() override { closeCalled = true; }
};

class Recorder final : public IRemotePlayback {
public:
    std::vector<std::string> calls;
    std::vector<RemoteTrackRef> lastItems;
    std::int64_t seekValue{-1};
    int volume{-1};

    void playNow(const std::vector<RemoteTrackRef>& items) override {
        calls.push_back("now");
        lastItems = items;
    }
    void playNext(const std::vector<RemoteTrackRef>& items) override {
        calls.push_back("next");
        lastItems = items;
    }
    void playLast(const std::vector<RemoteTrackRef>& items) override {
        calls.push_back("last");
        lastItems = items;
    }
    void stop() override { calls.push_back("stop"); }
    void pause() override { calls.push_back("pause"); }
    void unpause() override { calls.push_back("unpause"); }
    void nextTrack() override { calls.push_back("nextTrack"); }
    void previousTrack() override { calls.push_back("previousTrack"); }
    void seekMs(std::int64_t ms) override {
        calls.push_back("seek");
        seekValue = ms;
    }
    void setVolume(int value) override {
        calls.push_back("volume");
        volume = value;
    }
    void playPause() override { calls.push_back("playPause"); }
};

} // namespace

TEST_CASE("the socket endpoint keeps the token in the header only") {
    const RemoteEndpoint endpoint =
        remoteEndpoint("http://jellyfin.local:8096/", testProfile(), "secret-token", "device-1");
    CHECK(endpoint.url == "http://jellyfin.local:8096/socket");
    CHECK(endpoint.url.find("secret-token") == std::string::npos);
    CHECK(endpoint.url.find("api_key") == std::string::npos);

    bool found = false;
    for (const auto& [name, value] : endpoint.headers) {
        if (name == "Authorization") {
            found = value.find("Token=\"secret-token\"") != std::string::npos;
        }
    }
    CHECK(found);
}

TEST_CASE("the upgrade request authenticates with MediaBrowser, never a query key") {
    CurlWebSocketTransport transport{
        remoteEndpoint("https://jellyfin.example:8920", testProfile(), "secret-token", "d")};
    const std::string request = transport.handshakeRequest();

    CHECK(request.rfind("GET /socket HTTP/1.1\r\n", 0) == 0);
    CHECK(request.find("Host: jellyfin.example:8920") != std::string::npos);
    CHECK(request.find("Upgrade: websocket") != std::string::npos);
    CHECK(request.find("Sec-WebSocket-Key: ") != std::string::npos);
    CHECK(request.find("Sec-WebSocket-Version: 13") != std::string::npos);
    CHECK(request.find("Authorization: MediaBrowser") != std::string::npos);
    CHECK(request.find("Token=\"secret-token\"") != std::string::npos);
    CHECK(request.find("api_key") == std::string::npos);

    // The request line itself (what proxies log) carries no credential.
    const auto lineEnd = request.find("\r\n");
    CHECK(request.substr(0, lineEnd).find("secret-token") == std::string::npos);
}

TEST_CASE("the message catalog maps Play, Playstate and GeneralCommand") {
    auto message = parseRemoteMessage(
        R"({"MessageType":"Play","Data":{"PlayCommand":"PlayNext","ItemIds":["a","b"],"ItemNames":["A","B"],"StartPositionTicks":12000000}})");
    REQUIRE(message.has_value());
    CHECK(message->kind == RemoteMessageKind::Command);
    CHECK(message->command.kind == RemoteCommandKind::PlayNext);
    REQUIRE(message->command.items.size() == 2);
    CHECK(message->command.items[0].id == "a");
    CHECK(message->command.items[0].name == "A");
    CHECK(message->command.positionMs == 1200);

    message = parseRemoteMessage(
        R"({"MessageType":"Play","Data":{"PlayCommand":"PlayLast","ItemIds":["c"]}})");
    REQUIRE(message.has_value());
    CHECK(message->command.kind == RemoteCommandKind::PlayLast);

    message = parseRemoteMessage(
        R"({"MessageType":"Playstate","Data":{"Command":"Seek","SeekPositionTicks":50000000}})");
    REQUIRE(message.has_value());
    CHECK(message->command.kind == RemoteCommandKind::Seek);
    CHECK(message->command.positionMs == 5000);

    message = parseRemoteMessage(
        R"({"MessageType":"GeneralCommand","Data":{"Name":"SetVolume","Arguments":{"Volume":"42"}}})");
    REQUIRE(message.has_value());
    CHECK(message->command.kind == RemoteCommandKind::SetVolume);
    CHECK(message->command.volume100 == 42);

    message = parseRemoteMessage(
        R"({"MessageType":"GeneralCommand","Data":{"Name":"PlayPause"}})");
    REQUIRE(message.has_value());
    CHECK(message->command.kind == RemoteCommandKind::PlayPause);

    CHECK(parseRemoteMessage(R"({"MessageType":"ForceKeepAlive"})")->kind ==
          RemoteMessageKind::ForceKeepAlive);
    CHECK(parseRemoteMessage(R"({"MessageType":"ServerRestarting"})")->kind ==
          RemoteMessageKind::ServerRestarting);
    CHECK(parseRemoteMessage(R"({"MessageType":"UserDataChanged"})")->kind ==
          RemoteMessageKind::Other);
    CHECK_FALSE(parseRemoteMessage("not json").has_value());
}

TEST_CASE("the remote loop answers ForceKeepAlive and routes commands") {
    ScriptedTransport transport;
    Recorder recorder;
    transport.incoming = {
        R"({"MessageType":"ForceKeepAlive"})",
        R"({"MessageType":"Play","Data":{"PlayCommand":"PlayNext","ItemIds":["id-1"]}})",
        R"({"MessageType":"Playstate","Data":{"Command":"Pause"}})",
    };
    JellyfinRemoteClient client{transport, recorder};
    std::atomic<bool> stop{false};

    CHECK(client.run(stop) == RemoteCloseReason::Closed);
    CHECK(transport.connectCalled);
    CHECK(transport.closeCalled);
    REQUIRE(transport.sent.size() == 1);
    CHECK(transport.sent[0] == R"({"MessageType":"KeepAlive"})");
    REQUIRE(recorder.calls.size() == 2);
    CHECK(recorder.calls[0] == "next"); // enqueued, not a hijack
    CHECK(recorder.calls[1] == "pause");
    REQUIRE(recorder.lastItems.size() == 1);
    CHECK(recorder.lastItems[0].id == "id-1");
}

TEST_CASE("ServerRestarting closes the socket so the caller can back off") {
    ScriptedTransport transport;
    Recorder recorder;
    transport.incoming = {R"({"MessageType":"ServerRestarting"})"};
    JellyfinRemoteClient client{transport, recorder};
    std::atomic<bool> stop{false};

    CHECK(client.run(stop) == RemoteCloseReason::ServerRestarting);
    CHECK(transport.closeCalled);
    CHECK(recorder.calls.empty());
}

TEST_CASE("the reconnect backoff doubles and caps at 30 s") {
    CHECK(JellyfinRemoteClient::backoffFor(0).count() == 1000);
    CHECK(JellyfinRemoteClient::backoffFor(1).count() == 2000);
    CHECK(JellyfinRemoteClient::backoffFor(3).count() == 8000);
    CHECK(JellyfinRemoteClient::backoffFor(4).count() == 16000);
    CHECK(JellyfinRemoteClient::backoffFor(10).count() == 30000);
}

TEST_CASE("command routing distinguishes now, next and last") {
    Recorder recorder;
    RemoteCommand command;
    command.kind = RemoteCommandKind::PlayNext;
    command.items = {RemoteTrackRef{"x", "X"}};
    CHECK(routeRemoteCommand(command, recorder));
    CHECK(recorder.calls == std::vector<std::string>{"next"});

    command.kind = RemoteCommandKind::PlayNow;
    CHECK(routeRemoteCommand(command, recorder));
    command.kind = RemoteCommandKind::PlayLast;
    CHECK(routeRemoteCommand(command, recorder));
    CHECK(recorder.calls == std::vector<std::string>({"next", "now", "last"}));

    // An empty play list is a no-op, not a "stop everything".
    command.items.clear();
    CHECK_FALSE(routeRemoteCommand(command, recorder));

    command.kind = RemoteCommandKind::Seek;
    command.positionMs = 42000;
    CHECK(routeRemoteCommand(command, recorder));
    CHECK(recorder.seekValue == 42000);

    command.kind = RemoteCommandKind::SetVolume;
    command.volume100 = 65;
    CHECK(routeRemoteCommand(command, recorder));
    CHECK(recorder.volume == 65);
}
