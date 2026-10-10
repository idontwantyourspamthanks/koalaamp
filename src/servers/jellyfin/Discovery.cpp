// SPDX-License-Identifier: MIT
// jellyfin — UDP discovery. See Discovery.hpp.

#include "servers/jellyfin/Discovery.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace koalaamp::servers::jellyfin {
namespace {

using json = nlohmann::json;

constexpr std::size_t kBufferBytes = 4096;

std::string trim(std::string s) {
    const auto notSpace = [](unsigned char c) {
        return c != ' ' && c != '\t' && c != '\r' && c != '\n';
    };
    while (!s.empty() && !notSpace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && !notSpace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

void closeSocket(int fd) {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(fd));
#else
    ::close(fd);
#endif
}

} // namespace

std::optional<DiscoveredServer> parseDiscoveryReply(std::string_view bytes) {
    try {
        const json dto = json::parse(trim(std::string(bytes)));
        if (!dto.is_object()) {
            return std::nullopt;
        }
        auto address = trim(dto.value("Address", std::string{}));
        while (!address.empty() && address.back() == '/') {
            address.pop_back();
        }
        if (address.empty()) {
            return std::nullopt;
        }
        DiscoveredServer server;
        server.address = address;
        const std::string id = trim(dto.value("Id", std::string{}));
        const std::string name = trim(dto.value("Name", std::string{}));
        server.id = id.empty() ? address : id;
        server.name = name.empty() ? address : name;
        return server;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<DiscoveredServer> discoverServers(const DiscoveryConfig& config,
                                              const library::Cancellation& cancel) {
    std::vector<DiscoveredServer> found;
    const int fd = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (fd < 0) {
        return found;
    }

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));

    timeval tv{};
    tv.tv_sec = static_cast<long>(config.slice.count() / 1000);
    tv.tv_usec = static_cast<long>((config.slice.count() % 1000) * 1000);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    sockaddr_in broadcast{};
    broadcast.sin_family = AF_INET;
    broadcast.sin_port = htons(config.port);
    if (::inet_pton(AF_INET, config.broadcastAddress.c_str(), &broadcast.sin_addr) != 1) {
        closeSocket(fd);
        return found;
    }

    ::sendto(fd, kJellyfinProbe.data(), static_cast<int>(kJellyfinProbe.size()), 0,
             reinterpret_cast<const sockaddr*>(&broadcast), sizeof(broadcast));

    const auto deadline = std::chrono::steady_clock::now() + config.window;
    std::array<char, kBufferBytes> buffer{};
    while (!cancel.requested() && std::chrono::steady_clock::now() < deadline) {
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        const auto got = ::recvfrom(fd, buffer.data(), static_cast<int>(buffer.size()), 0,
                                    reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (got > 0) {
            if (auto server = parseDiscoveryReply(
                    std::string_view(buffer.data(), static_cast<std::size_t>(got)))) {
                // koalapaw emits every datagram; dedupe so a repeat reply does not
                // show up twice in the picker.
                const bool seen =
                    std::any_of(found.begin(), found.end(), [&](const DiscoveredServer& s) {
                        return s.id == server->id && s.address == server->address;
                    });
                if (!seen) {
                    found.push_back(std::move(*server));
                }
            }
        }
        // got <= 0: timeout slice elapsed (or error) — loop until the window closes.
    }

    closeSocket(fd);
    return found;
}

} // namespace koalaamp::servers::jellyfin
