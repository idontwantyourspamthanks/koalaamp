// SPDX-License-Identifier: MIT
// scrobble — the HTTP transport seam.
//
// Same shape as the Jellyfin gateway's seam (servers/jellyfin/HttpTransport.hpp)
// but deliberately independent: scrobbling must not pull the Jellyfin gateway —
// or its `library` types — into this module's public headers. Tests replay
// request/response pairs through a fake; production uses the adapter in
// CurlTransport.hpp, whose implementation wraps the existing libcurl transport.

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace koalaamp::scrobble {

struct HttpRequest {
    std::string method{"GET"};
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

struct HttpResponse {
    long status{0};
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    /// Empty on transport success. Non-empty means the request never produced a
    /// response ("connection refused", "timeout", …).
    std::string error;

    bool transportOk() const { return error.empty(); }
    bool httpOk() const { return transportOk() && status >= 200 && status < 300; }

    /// Case-insensitive header lookup; nullptr when absent.
    const std::string* header(std::string_view name) const {
        for (const auto& [key, value] : headers) {
            if (key.size() != name.size()) {
                continue;
            }
            bool same = true;
            for (std::size_t i = 0; i < name.size(); ++i) {
                const auto a = static_cast<unsigned char>(key[i]);
                const auto b = static_cast<unsigned char>(name[i]);
                const auto lowerA = (a >= 'A' && a <= 'Z') ? a - 'A' + 'a' : a;
                const auto lowerB = (b >= 'A' && b <= 'Z') ? b - 'A' + 'a' : b;
                if (lowerA != lowerB) {
                    same = false;
                    break;
                }
            }
            if (same) {
                return &value;
            }
        }
        return nullptr;
    }
};

/// Performs one HTTP request. Must not throw: failures come back as
/// `HttpResponse::error`.
class ITransport {
public:
    virtual ~ITransport() = default;
    virtual HttpResponse perform(const HttpRequest& request) = 0;
};

} // namespace koalaamp::scrobble
