// SPDX-License-Identifier: MIT
// jellyfin — the WebSocket transport seam (SV-2 remote control).
//
// Jellyfin pushes remote-control frames over an HTTP(S) upgrade at `/socket`.
// Authentication is the same MediaBrowser header as every other request: the
// token never goes into the URL (no query-string credential of any kind).
//
// `IWebSocketTransport` exists so the client loop is testable without a server:
// tests script frames through a fake and assert keepalives and routed commands.
// `CurlWebSocketTransport` speaks the protocol over a libcurl CONNECT_ONLY
// connection, which gives us TLS (and proxy support) without a second TLS stack.

#pragma once

#include "servers/jellyfin/AuthHeaders.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace koalaamp::servers::jellyfin {

/// Where to connect and which headers to present on the upgrade. Built by
/// `remoteEndpoint()`; kept separate so the auth policy is unit-testable.
struct RemoteEndpoint {
    std::string url; ///< http(s)://host/socket — curl performs the upgrade
    std::vector<std::pair<std::string, std::string>> headers;
};

/// `baseUrl` is the server root (http/https, optional port). The returned URL
/// carries no credentials and no query parameters; the Authorization header
/// does, exactly once.
RemoteEndpoint remoteEndpoint(const std::string& baseUrl, const DeviceProfile& profile,
                              const std::string& token, const std::string& deviceId);

class IWebSocketTransport {
public:
    virtual ~IWebSocketTransport() = default;

    /// Establishes the connection and completes the upgrade. Returns false and
    /// sets `error` on failure.
    virtual bool connect(std::string* error) = 0;

    /// Sends one text frame.
    virtual bool sendText(std::string_view text, std::string* error) = 0;

    /// Waits up to `timeoutMs` for a text frame. Returns true with `text` set on
    /// a message, false on timeout (`closed` stays false) or when the peer
    /// closed (`closed` set true). Pings are answered and control frames are
    /// consumed internally.
    virtual bool receive(std::string* text, int timeoutMs, bool* closed,
                         std::string* error) = 0;

    virtual void close() = 0;
};

/// Production transport: an HTTP(S) upgrade performed over `curl_easy_send`/
/// `curl_easy_recv` on a `CURLOPT_CONNECT_ONLY` handle, then RFC 6455 framing.
/// One connection per instance; call `connect()` again after `close()` to
/// reconnect.
class CurlWebSocketTransport final : public IWebSocketTransport {
public:
    explicit CurlWebSocketTransport(RemoteEndpoint endpoint);
    ~CurlWebSocketTransport() override;
    CurlWebSocketTransport(const CurlWebSocketTransport&) = delete;
    CurlWebSocketTransport& operator=(const CurlWebSocketTransport&) = delete;

    bool connect(std::string* error) override;
    bool sendText(std::string_view text, std::string* error) override;
    bool receive(std::string* text, int timeoutMs, bool* closed, std::string* error) override;
    void close() override;

    /// The upgrade request this transport would send, with a fresh key.
    /// Exposed for the auth-policy test (no token in the request line).
    std::string handshakeRequest() const;

private:
    struct Impl;
    Impl* impl_;
    RemoteEndpoint endpoint_;
};

} // namespace koalaamp::servers::jellyfin
