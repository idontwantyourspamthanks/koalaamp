// SPDX-License-Identifier: MIT
// jellyfin — the HTTP transport seam.
//
// koalapaw runs every call through one OkHttp client whose interceptor injects
// the MediaBrowser header. The C++ port keeps the same shape: the gateway owns
// no socket code, it hands an `HttpRequest` to an `IHttpTransport`. Production
// uses `CurlTransport` (libcurl); tests inject a fixture transport and replay
// recorded request/response pairs through the exact same code path.

#pragma once

#include "library/ITrackSource.hpp"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace koalaamp::servers::jellyfin {

struct HttpRequest {
    std::string method{"GET"};
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

struct HttpResponse {
    long status{0};
    std::string body;
    /// Empty on transport success. Non-empty means the request never produced a
    /// response ("canceled", "connection refused", …).
    std::string error;

    bool transportOk() const { return error.empty(); }
    bool httpOk() const { return transportOk() && status >= 200 && status < 300; }
};

/// A Cancellation that is never requested — the default for synchronous calls
/// that have no supersession semantics.
class NoCancellation final : public library::Cancellation {
public:
    bool requested() const override { return false; }
};

/// Performs one HTTP request. Must not throw: failures come back as
/// `HttpResponse::error` (koalapaw's IOException/Timeout → result-type mapping).
class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual HttpResponse perform(const HttpRequest& request,
                                 const library::Cancellation& cancel) = 0;

    HttpResponse perform(const HttpRequest& request) {
        NoCancellation none;
        return perform(request, none);
    }
};

/// libcurl-backed transport. One easy handle per call (spike scope); curl_global
/// is initialised lazily and never cleaned (process-lifetime, as libcurl intends).
class CurlTransport final : public IHttpTransport {
public:
    CurlTransport(std::chrono::milliseconds connectTimeout = std::chrono::seconds(5),
                  std::chrono::milliseconds totalTimeout = std::chrono::seconds(30));

    HttpResponse perform(const HttpRequest& request, const library::Cancellation& cancel) override;

private:
    std::chrono::milliseconds connectTimeout_;
    std::chrono::milliseconds totalTimeout_;
};

} // namespace koalaamp::servers::jellyfin
