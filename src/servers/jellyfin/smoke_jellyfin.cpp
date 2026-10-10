// SPDX-License-Identifier: MIT
// jellyfin_smoke — FE-5 live smoke: auth → UDP discovery → album browse.
//
// Usage:
//   jellyfin_smoke --url http://host:8096 --user NAME --password PW
//                  [--machine-id PATH] [--discover] [--auth-twice]
//                  [--parent ID] [--limit N] [--record DIR]
//
// Exit 0 = authenticated and listed a page of albums. Anything else prints the
// outcome variant and exits 1.

#include "servers/jellyfin/AuthHeaders.hpp"
#include "servers/jellyfin/DeviceIdentity.hpp"
#include "servers/jellyfin/Discovery.hpp"
#include "servers/jellyfin/FixtureTransport.hpp"
#include "servers/jellyfin/HttpTransport.hpp"
#include "servers/jellyfin/JellyfinClient.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

using namespace koalaamp::servers::jellyfin;

namespace {

std::string argValue(int argc, char** argv, const std::string& flag, const std::string& fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (flag == argv[i]) {
            return argv[i + 1];
        }
    }
    return fallback;
}

bool hasFlag(int argc, char** argv, const std::string& flag) {
    for (int i = 1; i < argc; ++i) {
        if (flag == argv[i]) {
            return true;
        }
    }
    return false;
}

std::string shortToken(const std::string& token) {
    return token.size() > 8 ? token.substr(0, 8) + "…" : token;
}

} // namespace

int main(int argc, char** argv) {
    const std::string user = argValue(argc, argv, "--user", "");
    const std::string password = argValue(argc, argv, "--password", "");
    std::string url = argValue(argc, argv, "--url", "");
    const std::string machineIdPath = argValue(argc, argv, "--machine-id", "");
    const std::string parentId = argValue(argc, argv, "--parent", "");
    const int limit = std::atoi(argValue(argc, argv, "--limit", "50").c_str());
    const std::string recordDir = argValue(argc, argv, "--record", "");

    if (user.empty() || password.empty()) {
        std::cerr << "usage: jellyfin_smoke --url URL --user NAME --password PW "
                     "[--machine-id PATH] [--discover] [--auth-twice] [--parent ID] "
                     "[--limit N] [--record DIR]\n";
        return 2;
    }

    MachineIdStore machineIdStore(machineIdPath);
    const std::string machineId = machineIdStore.machineId();
    const std::string deviceId = deriveDeviceId(machineId, user);
    std::cout << "machine-id:  " << machineIdStore.path() << " = " << machineId << "\n";
    std::cout << "device-name: " << defaultDeviceName() << "\n";
    std::cout << "device-id:   " << deviceId << "  (sha256(machineId+\":\"+user))\n";

    if (hasFlag(argc, argv, "--discover")) {
        DiscoveryConfig config;
        const std::string broadcast = argValue(argc, argv, "--broadcast", "");
        if (!broadcast.empty()) {
            config.broadcastAddress = broadcast;
        }
        const std::string window = argValue(argc, argv, "--window-ms", "2000");
        config.window = std::chrono::milliseconds(std::atoi(window.c_str()));
        std::cout << "discovery:   broadcasting \"" << kJellyfinProbe << "\" to "
                  << config.broadcastAddress << ":" << config.port << " …\n";
        const auto servers = discoverServers(config);
        if (servers.empty()) {
            std::cout << "discovery:   no servers answered (UDP may be blocked)\n";
        }
        for (const auto& server : servers) {
            std::cout << "discovery:   found name=\"" << server.name << "\" id=" << server.id
                      << " address=" << server.address << "\n";
        }
        if (url.empty() && !servers.empty()) {
            url = servers.front().address;
            std::cout << "discovery:   using " << url << "\n";
        }
    }
    if (url.empty()) {
        std::cerr << "error: no --url and discovery found nothing\n";
        return 2;
    }

    CurlTransport curl;
    std::unique_ptr<RecordingTransport> recorder;
    IHttpTransport* transport = &curl;
    if (!recordDir.empty()) {
        recorder = std::make_unique<RecordingTransport>(curl, recordDir);
        transport = recorder.get();
    }

    DeviceProfile profile;
    JellyfinClient client(*transport, profile);

    auto hold = std::make_shared<AuthSession>();
    const AuthOutcome auth = client.authenticate(url, user, password, deviceId);
    if (const auto* ok = std::get_if<AuthSuccess>(&auth)) {
        *hold = ok->session;
        std::cout << "auth#1:      ok user=\"" << hold->userName << "\" userId=" << hold->userId
                  << " serverId=" << hold->serverId << " token=" << shortToken(hold->accessToken)
                  << "\n";
    } else if (std::holds_alternative<AuthInvalidCredentials>(auth)) {
        std::cerr << "auth#1:      invalid credentials\n";
        return 1;
    } else if (std::holds_alternative<AuthUnreachable>(auth)) {
        std::cerr << "auth#1:      unreachable\n";
        return 1;
    } else {
        std::cerr << "auth#1:      failed: " << std::get<AuthFailed>(auth).message << "\n";
        return 1;
    }

    if (hasFlag(argc, argv, "--auth-twice")) {
        const AuthOutcome again = client.authenticate(url, user, password, deviceId);
        if (const auto* ok = std::get_if<AuthSuccess>(&again)) {
            std::cout << "auth#2:      ok (re-login, same device-id) token="
                      << shortToken(ok->session.accessToken) << " changed="
                      << (ok->session.accessToken != hold->accessToken ? "yes" : "no")
                      << "  [Jellyfin keeps one token per DeviceId, so the first is now stale]\n";
            // The trap: re-authenticating with the same id evicts the previous
            // token. Adopt the fresh one before any further call.
            *hold = ok->session;
        } else {
            std::cerr << "auth#2:      FAILED — device-id reused invalidated the token\n";
            return 1;
        }
    }

    const ItemsOutcome items =
        client.musicAlbumsWindow(url, hold->accessToken, deviceId, parentId, 0, limit);
    if (const auto* ok = std::get_if<ItemsSuccess>(&items)) {
        std::cout << "albums:      total=" << ok->window.totalRecordCount
                  << " startIndex=" << ok->window.startIndex
                  << " returned=" << ok->window.items.size() << "\n";
        for (const auto& album : ok->window.items) {
            std::cout << "  - " << album.name;
            if (!album.albumArtist.empty()) {
                std::cout << " — " << album.albumArtist;
            }
            if (album.productionYear > 0) {
                std::cout << " (" << album.productionYear << ")";
            }
            std::cout << "  [" << album.id << "]\n";
        }
    } else if (std::holds_alternative<ItemsUnauthorized>(items)) {
        std::cerr << "albums:      unauthorized (token rejected)\n";
        return 1;
    } else if (std::holds_alternative<ItemsUnreachable>(items)) {
        std::cerr << "albums:      unreachable\n";
        return 1;
    } else {
        std::cerr << "albums:      failed: " << std::get<ItemsFailed>(items).message << "\n";
        return 1;
    }

    return 0;
}
