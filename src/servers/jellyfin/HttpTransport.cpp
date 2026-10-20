// SPDX-License-Identifier: MIT
// jellyfin — the HTTP transport seam. See HttpTransport.hpp.

#include "servers/jellyfin/HttpTransport.hpp"

#include <curl/curl.h>
#include <mutex>

namespace koalaamp::servers::jellyfin {
namespace {

void ensureCurlGlobal() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::size_t writeBody(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

struct ProgressContext {
    const library::Cancellation* cancel;
};

int onProgress(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* ctx = static_cast<const ProgressContext*>(clientp);
    return (ctx->cancel != nullptr && ctx->cancel->requested()) ? 1 : 0;
}

} // namespace

CurlTransport::CurlTransport(std::chrono::milliseconds connectTimeout,
                             std::chrono::milliseconds totalTimeout)
    : connectTimeout_(connectTimeout), totalTimeout_(totalTimeout) {}

namespace {

/// Response headers, final hop only: a status line starts a new block (redirect
/// hops included), so clearing on it leaves the last response's headers.
std::size_t onHeader(char* data, std::size_t size, std::size_t nmemb, void* userp) {
    const std::size_t total = size * nmemb;
    auto* headers = static_cast<std::vector<std::pair<std::string, std::string>>*>(userp);
    const std::string line(data, total);
    if (line.rfind("HTTP/", 0) == 0) {
        headers->clear();
        return total;
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        return total;
    }
    const auto trim = [](std::string_view in) {
        while (!in.empty() && (in.front() == ' ' || in.front() == '\t')) {
            in.remove_prefix(1);
        }
        while (!in.empty() && (in.back() == '\r' || in.back() == '\n' || in.back() == ' ')) {
            in.remove_suffix(1);
        }
        return std::string(in);
    };
    headers->emplace_back(trim(std::string_view(line).substr(0, colon)),
                          trim(std::string_view(line).substr(colon + 1)));
    return total;
}

} // namespace

HttpResponse CurlTransport::perform(const HttpRequest& request,
                                    const library::Cancellation& cancel) {
    ensureCurlGlobal();

    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        response.error = "curl_easy_init failed";
        return response;
    }

    char errbuf[CURL_ERROR_SIZE] = {};
    curl_slist* headers = nullptr;
    for (const auto& [name, value] : request.headers) {
        const std::string line = name + ": " + value;
        headers = curl_slist_append(headers, line.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &writeBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &onHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    const auto connectTimeout =
        request.connectTimeout.count() > 0 ? request.connectTimeout : connectTimeout_;
    const auto totalTimeout = request.totalTimeout.count() > 0 ? request.totalTimeout : totalTimeout_;
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(connectTimeout.count()));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(totalTimeout.count()));

    ProgressContext progress{&cancel};
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &onProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);

    if (request.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (!request.method.empty() && request.method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    }
    if (!request.body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(request.body.size()));
    }

    const CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_ABORTED_BY_CALLBACK) {
        response.error = "canceled";
    } else if (code != CURLE_OK) {
        response.error = errbuf[0] != '\0' ? errbuf : curl_easy_strerror(code);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

} // namespace koalaamp::servers::jellyfin
