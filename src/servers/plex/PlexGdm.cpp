// SPDX-License-Identifier: MIT
// plex — GDM discovery. See PlexGdm.hpp.
//
// Ported from koalapaw `GdmServerDiscovery.kt`: probe `M-SEARCH * HTTP/1.1` on
// UDP 32414 (flat broadcast and the GDM multicast group), parse the header-style
// replies, and use the packet's source address as the server host.

#include "servers/plex/PlexGdm.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <vector>

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

namespace koalaamp::servers::plex {
namespace {

constexpr std::uint16_t kGdmPort = 32414;
constexpr std::uint16_t kDefaultMediaPort = 32400;
constexpr std::size_t kBufferBytes = 4096;
constexpr std::string_view kProbe = "M-SEARCH * HTTP/1.1\r\n\r\n";

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char c) {
        return c != ' ' && c != '\t' && c != '\r' && c != '\n';
    };
    while (!value.empty() && !notSpace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && !notSpace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

void closeSocket(int fd) {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(fd));
#else
    ::close(fd);
#endif
}

class WinsockGuard {
public:
    WinsockGuard() {
#if defined(_WIN32)
        initialized_ = ::WSAStartup(MAKEWORD(2, 2), &data_) == 0;
#endif
    }
    ~WinsockGuard() {
#if defined(_WIN32)
        if (initialized_) {
            ::WSACleanup();
        }
#endif
    }
    WinsockGuard(const WinsockGuard&) = delete;
    WinsockGuard& operator=(const WinsockGuard&) = delete;

private:
#if defined(_WIN32)
    bool initialized_{false};
    WSADATA data_{};
#endif
};

std::string gdmField(std::string_view text, std::string_view label) {
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        if (line.size() >= label.size() && line.substr(0, label.size()) == label) {
            return trim(std::string(line.substr(label.size())));
        }
        start = end + 1;
    }
    return {};
}

} // namespace

std::optional<PlexDiscoveredServer> parseGdmReply(std::string_view bytes,
                                                  const std::string& host) {
    std::string text(bytes);
    const std::string name = gdmField(text, "Name:");
    const std::string id = gdmField(text, "Resource-Identifier:");
    if (name.empty() || id.empty()) {
        return std::nullopt;
    }
    int port = kDefaultMediaPort;
    const std::string portText = gdmField(text, "Port:");
    if (!portText.empty()) {
        int parsed = 0;
        const auto result = std::from_chars(portText.data(), portText.data() + portText.size(),
                                            parsed);
        if (result.ec == std::errc{} && parsed >= 1 && parsed <= 65535) {
            port = parsed;
        }
    }
    return PlexDiscoveredServer{id, name, "http://" + host + ":" + std::to_string(port)};
}

std::vector<PlexDiscoveredServer> discoverPlexServers(std::chrono::milliseconds window,
                                                      const library::Cancellation& cancel) {
    std::vector<PlexDiscoveredServer> found;
    const WinsockGuard winsock;
    const int fd = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (fd < 0) {
        return found;
    }
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 300000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                 sizeof(timeout));

    const auto sendProbe = [fd](const char* address) {
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(kGdmPort);
        if (::inet_pton(AF_INET, address, &target.sin_addr) == 1) {
            ::sendto(fd, kProbe.data(), static_cast<int>(kProbe.size()), 0,
                     reinterpret_cast<const sockaddr*>(&target), sizeof(target));
        }
    };
    sendProbe("255.255.255.255");
    sendProbe("239.0.0.250"); // the GDM multicast group; some networks only honour this one

    const auto deadline = std::chrono::steady_clock::now() + window;
    std::array<char, kBufferBytes> buffer{};
    while (std::chrono::steady_clock::now() < deadline && !cancel.requested()) {
        sockaddr_in from{};
#if defined(_WIN32)
        int fromLength = sizeof(from);
#else
        socklen_t fromLength = sizeof(from);
#endif
        const int received =
            static_cast<int>(::recvfrom(fd, buffer.data(), static_cast<int>(buffer.size()), 0,
                                        reinterpret_cast<sockaddr*>(&from), &fromLength));
        if (received <= 0) {
            continue; // slice timeout
        }
        char host[INET_ADDRSTRLEN] = {};
        if (::inet_ntop(AF_INET, &from.sin_addr, host, sizeof(host)) == nullptr) {
            continue;
        }
        if (auto server = parseGdmReply(std::string_view(buffer.data(), received), host)) {
            const bool duplicate =
                std::any_of(found.begin(), found.end(),
                            [&](const PlexDiscoveredServer& existing) {
                                return existing.id == server->id;
                            });
            if (!duplicate) {
                found.push_back(std::move(*server));
            }
        }
    }
    closeSocket(fd);
    return found;
}

} // namespace koalaamp::servers::plex
