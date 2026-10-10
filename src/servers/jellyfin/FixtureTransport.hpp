// SPDX-License-Identifier: MIT
// jellyfin — recorded-fixture transport.
//
// koalapaw's tests replay gateway calls through OkHttp's MockWebServer and
// assert the interceptor's header. The C++ twin is this: `RecordingTransport`
// captures live request/response pairs to JSON files; `FixtureTransport`
// replays them through the exact same `JellyfinClient` path with no network.
// One exchange per file, so a recorded corpus is diffable in review.

#pragma once

#include "servers/jellyfin/HttpTransport.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace koalaamp::servers::jellyfin {

/// One captured round-trip.
struct RecordedExchange {
    HttpRequest request;
    HttpResponse response;
};

/// Replays recorded exchanges. Requests are matched to exchanges by
/// (method, url); the first unconsumed match wins. Every `perform` call is
/// recorded in `requests()` for assertions.
class FixtureTransport final : public IHttpTransport {
public:
    /// Loads every `*.json` in `dir`, sorted by file name.
    explicit FixtureTransport(const std::filesystem::path& dir);

    HttpResponse perform(const HttpRequest& request, const library::Cancellation& cancel) override;

    const std::vector<HttpRequest>& requests() const { return requests_; }
    const std::vector<RecordedExchange>& exchanges() const { return exchanges_; }

private:
    std::vector<RecordedExchange> exchanges_;
    std::vector<bool> consumed_;
    std::vector<HttpRequest> requests_;
};

/// Wraps a transport and writes each exchange to `dir/<NNN>-<slug>.json`.
class RecordingTransport final : public IHttpTransport {
public:
    RecordingTransport(IHttpTransport& inner, std::filesystem::path dir);

    HttpResponse perform(const HttpRequest& request, const library::Cancellation& cancel) override;

private:
    IHttpTransport& inner_;
    std::filesystem::path dir_;
    int next_{0};
};

/// Serialise/deserialise one exchange (public so the smoke's `--record` and the
/// tests share one format).
std::string serializeExchange(const RecordedExchange& exchange);
RecordedExchange parseExchange(const std::string& json);

} // namespace koalaamp::servers::jellyfin
