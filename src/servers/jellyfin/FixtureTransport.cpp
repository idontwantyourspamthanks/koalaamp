// SPDX-License-Identifier: MIT
// jellyfin — recorded-fixture transport. See FixtureTransport.hpp.

#include "servers/jellyfin/FixtureTransport.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace koalaamp::servers::jellyfin {
namespace {

using json = nlohmann::json;

std::string slugFor(const HttpRequest& request) {
    std::string url = request.url;
    if (const auto scheme = url.find("://"); scheme != std::string::npos) {
        url.erase(0, scheme + 3);
    }
    std::string slug;
    for (const char c : url) {
        slug.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '-');
    }
    while (!slug.empty() && slug.front() == '-') {
        slug.erase(slug.begin());
    }
    std::string full = request.method + "-" + slug;
    if (full.size() > 80) {
        full.resize(80);
    }
    return full;
}

} // namespace

namespace {

/// Embed a serialized payload as a real JSON value when it parses, so a fixture
/// file stays human-readable and diffable; fall back to a plain string.
json embedPayload(const std::string& text) {
    if (text.empty()) {
        return json{""};
    }
    if (json parsed = json::parse(text, nullptr, false); !parsed.is_discarded()) {
        return parsed;
    }
    return json{text};
}

std::string flattenPayload(const json& value) {
    return value.is_string() ? value.get<std::string>() : value.dump();
}

} // namespace

std::string serializeExchange(const RecordedExchange& exchange) {
    json j;
    j["request"]["method"] = exchange.request.method;
    j["request"]["url"] = exchange.request.url;
    j["request"]["headers"] = exchange.request.headers;
    j["request"]["body"] = embedPayload(exchange.request.body);
    j["response"]["status"] = exchange.response.status;
    j["response"]["body"] = embedPayload(exchange.response.body);
    j["response"]["error"] = exchange.response.error;
    return j.dump(2) + "\n";
}

RecordedExchange parseExchange(const std::string& text) {
    const json j = json::parse(text);
    RecordedExchange exchange;
    const auto& req = j.at("request");
    exchange.request.method = req.value("method", std::string{"GET"});
    exchange.request.url = req.value("url", std::string{});
    if (const auto it = req.find("headers"); it != req.end() && it->is_array()) {
        for (const auto& pair : *it) {
            if (pair.is_array() && pair.size() == 2 && pair[0].is_string() && pair[1].is_string()) {
                exchange.request.headers.emplace_back(pair[0].get<std::string>(),
                                                      pair[1].get<std::string>());
            }
        }
    }
    if (const auto it = req.find("body"); it != req.end()) {
        exchange.request.body = flattenPayload(*it);
    }
    const auto& res = j.at("response");
    exchange.response.status = res.value("status", 0L);
    if (const auto it = res.find("body"); it != res.end()) {
        exchange.response.body = flattenPayload(*it);
    }
    exchange.response.error = res.value("error", std::string{});
    return exchange;
}

FixtureTransport::FixtureTransport(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::ostringstream buf;
        buf << in.rdbuf();
        exchanges_.push_back(parseExchange(buf.str()));
    }
    consumed_.assign(exchanges_.size(), false);
}

HttpResponse FixtureTransport::perform(const HttpRequest& request, const library::Cancellation&) {
    requests_.push_back(request);
    for (std::size_t i = 0; i < exchanges_.size(); ++i) {
        if (!consumed_[i] && exchanges_[i].request.method == request.method &&
            exchanges_[i].request.url == request.url) {
            consumed_[i] = true;
            return exchanges_[i].response;
        }
    }
    HttpResponse miss;
    miss.error = "no recorded fixture for " + request.method + " " + request.url;
    return miss;
}

RecordingTransport::RecordingTransport(IHttpTransport& inner, std::filesystem::path dir)
    : inner_(inner), dir_(std::move(dir)) {}

HttpResponse RecordingTransport::perform(const HttpRequest& request,
                                         const library::Cancellation& cancel) {
    const HttpResponse response = inner_.perform(request, cancel);
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);

    std::ostringstream name;
    name << (next_ < 10 ? "0" : "") << next_ << "-" << slugFor(request) << ".json";
    ++next_;

    std::ofstream out(dir_ / name.str(), std::ios::binary | std::ios::trunc);
    out << serializeExchange(RecordedExchange{request, response});
    return response;
}

} // namespace koalaamp::servers::jellyfin
