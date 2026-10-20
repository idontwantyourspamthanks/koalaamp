// SPDX-License-Identifier: MIT
// jellyfin — RFC 6455 client over a libcurl CONNECT_ONLY connection.
//
// curl does the hard part (TCP, TLS, proxies); after `CURLOPT_CONNECT_ONLY` it
// hands us the socket and `curl_easy_send`/`curl_easy_recv` stay encrypted-aware.
// We then perform the HTTP upgrade ourselves and speak WebSocket frames. The
// token rides the ordinary MediaBrowser header on the upgrade request.

#include "servers/jellyfin/WebSocket.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace koalaamp::servers::jellyfin {
namespace {

using Clock = std::chrono::steady_clock;

std::string randomBytes(std::size_t count) {
    static std::random_device device;
    std::string out(count, '\0');
    for (char& byte : out) {
        byte = static_cast<char>(device() & 0xffu);
    }
    return out;
}

std::string base64(const std::uint8_t* data, std::size_t length) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((length + 2) / 3 * 4);
    for (std::size_t i = 0; i < length; i += 3) {
        std::uint32_t value = static_cast<std::uint32_t>(data[i]) << 16;
        if (i + 1 < length) {
            value |= static_cast<std::uint32_t>(data[i + 1]) << 8;
        }
        if (i + 2 < length) {
            value |= data[i + 2];
        }
        out.push_back(table[(value >> 18) & 0x3f]);
        out.push_back(table[(value >> 12) & 0x3f]);
        out.push_back(i + 1 < length ? table[(value >> 6) & 0x3f] : '=');
        out.push_back(i + 2 < length ? table[value & 0x3f] : '=');
    }
    return out;
}

std::string base64(std::string_view bytes) {
    return base64(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

/// Minimal SHA-1 (FIPS 180-1) for the `Sec-WebSocket-Accept` check.
std::array<std::uint8_t, 20> sha1(std::string_view text) {
    auto rol = [](std::uint32_t value, int bits) {
        return static_cast<std::uint32_t>((value << bits) | (value >> (32 - bits)));
    };
    std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<std::uint8_t> message(text.begin(), text.end());
    const std::uint64_t bits = static_cast<std::uint64_t>(text.size()) * 8u;
    message.push_back(0x80);
    while (message.size() % 64 != 56) {
        message.push_back(0);
    }
    for (int i = 7; i >= 0; --i) {
        message.push_back(static_cast<std::uint8_t>((bits >> (i * 8)) & 0xffu));
    }
    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(message[offset + i * 4]) << 24) |
                   (static_cast<std::uint32_t>(message[offset + i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(message[offset + i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(message[offset + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const std::uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    std::array<std::uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = static_cast<std::uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(h[i]);
    }
    return out;
}

std::string acceptKey(std::string_view key) {
    const std::string magic = std::string(key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const auto digest = sha1(magic);
    return base64(digest.data(), digest.size());
}

struct ParsedUrl {
    std::string hostPort;
    std::string path;
};

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl parsed;
    const auto scheme = url.find("://");
    const std::string rest = scheme == std::string::npos ? url : url.substr(scheme + 3);
    const auto slash = rest.find('/');
    parsed.hostPort = rest.substr(0, slash);
    parsed.path = slash == std::string::npos ? "/" : rest.substr(slash);
    return parsed;
}

std::string buildHandshake(const RemoteEndpoint& endpoint, std::string_view key) {
    const ParsedUrl parsed = parseUrl(endpoint.url);
    std::string request = "GET " + parsed.path + " HTTP/1.1\r\n";
    request += "Host: " + parsed.hostPort + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + std::string(key) + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    for (const auto& [name, value] : endpoint.headers) {
        request += name + ": " + value + "\r\n";
    }
    request += "\r\n";
    return request;
}

bool sendAll(CURL* easy, std::string_view bytes, std::string* error) {
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        std::size_t chunk = 0;
        const CURLcode rc = curl_easy_send(easy, bytes.data() + sent, bytes.size() - sent, &chunk);
        if (rc == CURLE_OK) {
            sent += chunk;
            continue;
        }
        if (rc != CURLE_AGAIN) {
            if (error != nullptr) {
                *error = "WebSocket send failed: " + std::string(curl_easy_strerror(rc));
            }
            return false;
        }
        if (Clock::now() >= deadline) {
            if (error != nullptr) {
                *error = "WebSocket send timed out";
            }
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

/// 1 = read, 0 = timed out, -1 = closed/error.
int readExact(CURL* easy, void* buffer, std::size_t length, Clock::time_point deadline) {
    auto* out = static_cast<std::uint8_t*>(buffer);
    std::size_t got = 0;
    while (got < length) {
        std::size_t chunk = 0;
        const CURLcode rc = curl_easy_recv(easy, out + got, length - got, &chunk);
        if (rc == CURLE_OK) {
            if (chunk == 0) {
                return -1;
            }
            got += chunk;
            continue;
        }
        if (rc != CURLE_AGAIN) {
            return -1;
        }
        if (Clock::now() >= deadline) {
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 1;
}

bool sendFrame(CURL* easy, std::uint8_t opcode, std::string_view payload, std::string* error) {
    std::string frame;
    frame.push_back(static_cast<char>(0x80u | opcode));
    const std::size_t length = payload.size();
    if (length < 126) {
        frame.push_back(static_cast<char>(0x80u | static_cast<std::uint8_t>(length)));
    } else if (length <= 0xffff) {
        frame.push_back(static_cast<char>(0x80u | 126u));
        frame.push_back(static_cast<char>((length >> 8) & 0xffu));
        frame.push_back(static_cast<char>(length & 0xffu));
    } else {
        frame.push_back(static_cast<char>(0x80u | 127u));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((static_cast<std::uint64_t>(length) >> (i * 8)) & 0xffu));
        }
    }
    const std::string mask = randomBytes(4);
    frame += mask;
    for (std::size_t i = 0; i < length; ++i) {
        frame.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));
    }
    return sendAll(easy, frame, error);
}

bool containsIgnoreCase(std::string_view haystack, std::string_view needle) {
    const auto it = std::search(
        haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

} // namespace

RemoteEndpoint remoteEndpoint(const std::string& baseUrl, const DeviceProfile& profile,
                              const std::string& token, const std::string& deviceId) {
    RemoteEndpoint endpoint;
    std::string base = baseUrl;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    // curl performs the upgrade over HTTP(S); the framing on top is WebSocket.
    endpoint.url = base + "/socket";
    endpoint.headers = baseHeaders(profile, deviceId, token);
    return endpoint;
}

struct CurlWebSocketTransport::Impl {
    CURL* easy{nullptr};
    bool open{false};
};

CurlWebSocketTransport::CurlWebSocketTransport(RemoteEndpoint endpoint)
    : impl_(new Impl), endpoint_(std::move(endpoint)) {}

CurlWebSocketTransport::~CurlWebSocketTransport() {
    close();
    delete impl_;
}

std::string CurlWebSocketTransport::handshakeRequest() const {
    return buildHandshake(endpoint_, base64(randomBytes(16)));
}

bool CurlWebSocketTransport::connect(std::string* error) {
    close();
    CURL* easy = curl_easy_init();
    if (easy == nullptr) {
        if (error != nullptr) {
            *error = "curl_easy_init failed";
        }
        return false;
    }
    curl_easy_setopt(easy, CURLOPT_URL, endpoint_.url.c_str());
    curl_easy_setopt(easy, CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);

    const CURLcode rc = curl_easy_perform(easy);
    if (rc != CURLE_OK) {
        if (error != nullptr) {
            *error = "WebSocket connect failed: " + std::string(curl_easy_strerror(rc));
        }
        curl_easy_cleanup(easy);
        return false;
    }

    // The upgrade uses a fresh key per connection; remember it for the accept
    // check (a server that does not echo it is not our server).
    const std::string key = base64(randomBytes(16));
    const std::string request = buildHandshake(endpoint_, key);
    if (!sendAll(easy, request, error)) {
        curl_easy_cleanup(easy);
        return false;
    }

    std::string response;
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (response.find("\r\n\r\n") == std::string::npos) {
        char byte = 0;
        if (readExact(easy, &byte, 1, deadline) != 1) {
            if (error != nullptr) {
                *error = "No WebSocket upgrade response";
            }
            curl_easy_cleanup(easy);
            return false;
        }
        response.push_back(byte);
        if (response.size() > 16384) {
            if (error != nullptr) {
                *error = "WebSocket upgrade response too large";
            }
            curl_easy_cleanup(easy);
            return false;
        }
    }

    if (response.find(" 101 ") == std::string::npos) {
        const auto lineEnd = response.find("\r\n");
        if (error != nullptr) {
            *error = "WebSocket upgrade refused: " + response.substr(0, lineEnd);
        }
        curl_easy_cleanup(easy);
        return false;
    }
    if (!containsIgnoreCase(response, acceptKey(key))) {
        if (error != nullptr) {
            *error = "WebSocket upgrade failed the Sec-WebSocket-Accept check";
        }
        curl_easy_cleanup(easy);
        return false;
    }

    impl_->easy = easy;
    impl_->open = true;
    return true;
}

bool CurlWebSocketTransport::sendText(std::string_view text, std::string* error) {
    if (!impl_->open || impl_->easy == nullptr) {
        if (error != nullptr) {
            *error = "WebSocket is not connected";
        }
        return false;
    }
    return sendFrame(impl_->easy, 0x1, text, error);
}

bool CurlWebSocketTransport::receive(std::string* text, int timeoutMs, bool* closed,
                                     std::string* error) {
    if (closed != nullptr) {
        *closed = false;
    }
    if (!impl_->open || impl_->easy == nullptr) {
        if (closed != nullptr) {
            *closed = true;
        }
        return false;
    }
    CURL* easy = impl_->easy;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    std::string fragmented;

    for (;;) {
        std::uint8_t header[2];
        int result = readExact(easy, header, 2, deadline);
        if (result == 0) {
            return false; // idle timeout, still connected
        }
        if (result < 0) {
            if (closed != nullptr) {
                *closed = true;
            }
            return false;
        }
        const bool final = (header[0] & 0x80u) != 0;
        const std::uint8_t opcode = header[0] & 0x0fu;
        const bool masked = (header[1] & 0x80u) != 0;
        std::uint64_t length = header[1] & 0x7fu;
        if (length == 126) {
            std::uint8_t extended[2];
            if (readExact(easy, extended, 2, deadline) != 1) {
                if (closed != nullptr) {
                    *closed = true;
                }
                return false;
            }
            length = (static_cast<std::uint64_t>(extended[0]) << 8) | extended[1];
        } else if (length == 127) {
            std::uint8_t extended[8];
            if (readExact(easy, extended, 8, deadline) != 1) {
                if (closed != nullptr) {
                    *closed = true;
                }
                return false;
            }
            length = 0;
            for (int i = 0; i < 8; ++i) {
                length = (length << 8) | extended[i];
            }
        }
        if (length > 4u * 1024u * 1024u) {
            if (error != nullptr) {
                *error = "WebSocket frame too large";
            }
            if (closed != nullptr) {
                *closed = true;
            }
            return false;
        }
        std::uint8_t mask[4] = {};
        if (masked && readExact(easy, mask, 4, deadline) != 1) {
            if (closed != nullptr) {
                *closed = true;
            }
            return false;
        }
        std::string payload(static_cast<std::size_t>(length), '\0');
        if (length > 0 && readExact(easy, payload.data(), payload.size(), deadline) != 1) {
            if (closed != nullptr) {
                *closed = true;
            }
            return false;
        }
        if (masked) {
            for (std::size_t i = 0; i < payload.size(); ++i) {
                payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
            }
        }

        switch (opcode) {
            case 0x0: // continuation
                fragmented += payload;
                if (final) {
                    *text = std::move(fragmented);
                    return true;
                }
                break;
            case 0x1: // text
            case 0x2: // binary (Jellyfin sends text; accept both)
                if (!final) {
                    fragmented = std::move(payload);
                    break;
                }
                *text = std::move(payload);
                return true;
            case 0x8: // close
                sendFrame(easy, 0x8, payload.substr(0, 2), nullptr);
                if (closed != nullptr) {
                    *closed = true;
                }
                return false;
            case 0x9: // ping
                sendFrame(easy, 0xA, payload, nullptr);
                break;
            case 0xA: // pong
                break;
            default:
                break;
        }
    }
}

void CurlWebSocketTransport::close() {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->easy != nullptr) {
        if (impl_->open) {
            sendFrame(impl_->easy, 0x8, {}, nullptr);
        }
        curl_easy_cleanup(impl_->easy);
        impl_->easy = nullptr;
    }
    impl_->open = false;
}

} // namespace koalaamp::servers::jellyfin
