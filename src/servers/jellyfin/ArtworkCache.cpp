// SPDX-License-Identifier: MIT
// jellyfin — primary artwork fetch with a disk cache. See ArtworkCache.hpp.

#include "servers/jellyfin/ArtworkCache.hpp"

#include <cctype>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string_view>
#include <system_error>
#include <utility>

namespace koalaamp::servers::jellyfin {
namespace {

using json = nlohmann::json;
using Clock = std::chrono::system_clock;

long long nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch())
        .count();
}

std::string baseUrlNoSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

/// The extension for the file, from the response's `Content-Type`.
std::string extensionFor(const std::string& contentType) {
    if (contentType.find("png") != std::string::npos) {
        return ".png";
    }
    if (contentType.find("jpeg") != std::string::npos ||
        contentType.find("jpg") != std::string::npos) {
        return ".jpg";
    }
    if (contentType.find("webp") != std::string::npos) {
        return ".webp";
    }
    if (contentType.find("gif") != std::string::npos) {
        return ".gif";
    }
    return ".img";
}

} // namespace

long ArtworkCache::maxAgeSeconds(const std::string& cacheControl) {
    std::string lower;
    lower.reserve(cacheControl.size());
    for (const char c : cacheControl) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower.find("no-store") != std::string::npos ||
        lower.find("no-cache") != std::string::npos) {
        return -1;
    }
    const auto at = lower.find("max-age=");
    if (at == std::string::npos) {
        return -1;
    }
    const std::size_t first = at + std::string_view{"max-age="}.size();
    std::size_t last = first;
    while (last < lower.size() && std::isdigit(static_cast<unsigned char>(lower[last])) != 0) {
        ++last;
    }
    if (last == first) {
        return -1;
    }
    try {
        return std::stol(lower.substr(first, last - first));
    } catch (const std::exception&) {
        return -1;
    }
}

ArtworkCache::ArtworkCache(IHttpTransport& transport, DeviceProfile profile,
                           std::filesystem::path dir)
    : transport_(transport), profile_(std::move(profile)), dir_(std::move(dir)) {}

std::optional<std::filesystem::path>
ArtworkCache::primaryImage(const std::string& baseUrl, const std::string& token,
                           const std::string& deviceId, const std::string& itemId,
                           const std::string& imageTag, const library::Cancellation& cancel) {
    // Jellyfin ids and tags are hex, so they need no query encoding.
    const std::string key = itemId + "-" + (imageTag.empty() ? "untagged" : imageTag);
    const auto meta = dir_ / (key + ".meta");

    // A fresh copy: within the recorded max-age, with the file still on disk.
    {
        std::ifstream in(meta);
        if (in) {
            try {
                const json j = json::parse(in);
                const long long fetchedAt = j.value("fetchedAt", 0LL);
                const long maxAge = j.value("maxAge", -1L);
                const std::string ext = j.value("ext", std::string{".img"});
                std::error_code ec;
                const auto file = dir_ / (key + ext);
                if (maxAge >= 0 && std::filesystem::exists(file, ec) &&
                    nowSeconds() - fetchedAt < static_cast<long long>(maxAge)) {
                    return file;
                }
            } catch (const std::exception&) {
                // A malformed sidecar is a miss: refetch below.
            }
        }
    }

    const std::string tagQuery = imageTag.empty() ? std::string{} : "?tag=" + imageTag;
    HttpRequest request;
    request.method = "GET";
    request.url = baseUrlNoSlash(baseUrl) + "/Items/" + itemId + "/Images/Primary" + tagQuery;
    request.headers = baseHeaders(profile_, deviceId, token);

    const HttpResponse response = transport_.perform(request, cancel);
    if (!response.transportOk() || !response.httpOk() || response.body.empty()) {
        return std::nullopt;
    }

    const std::string* contentType = response.header("Content-Type");
    const std::string ext = extensionFor(contentType != nullptr ? *contentType : std::string{});
    const auto file = dir_ / (key + ext);

    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    {
        std::ofstream out(file, std::ios::binary);
        out.write(response.body.data(), static_cast<std::streamsize>(response.body.size()));
        if (!out) {
            return std::nullopt;
        }
    }

    const std::string* cacheControl = response.header("Cache-Control");
    json metaJson;
    metaJson["fetchedAt"] = nowSeconds();
    metaJson["maxAge"] = maxAgeSeconds(cacheControl != nullptr ? *cacheControl : std::string{});
    metaJson["ext"] = ext;
    std::ofstream out(meta);
    out << metaJson.dump(2) << "\n";

    return file;
}

} // namespace koalaamp::servers::jellyfin
