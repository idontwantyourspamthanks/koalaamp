// SPDX-License-Identifier: MIT
// scrobble — production HTTP transport factory. See CurlTransport.hpp.
//
// This is the only translation unit in the module that includes the Jellyfin
// gateway's transport header; it is an implementation detail, not a public
// header, and the conversion is mechanical.

#include "scrobble/CurlTransport.hpp"

#include "servers/jellyfin/HttpTransport.hpp"

#include <utility>

namespace koalaamp::scrobble {
namespace {

class CurlAdapter final : public ITransport {
public:
    CurlAdapter(std::chrono::milliseconds connectTimeout,
                std::chrono::milliseconds totalTimeout)
        : inner_(connectTimeout, totalTimeout) {}

    HttpResponse perform(const HttpRequest& request) override {
        servers::jellyfin::HttpRequest converted;
        converted.method = request.method;
        converted.url = request.url;
        converted.headers = request.headers;
        converted.body = request.body;

        servers::jellyfin::NoCancellation noCancel;
        servers::jellyfin::HttpResponse response = inner_.perform(converted, noCancel);

        HttpResponse out;
        out.status = response.status;
        out.body = std::move(response.body);
        out.headers = std::move(response.headers);
        out.error = std::move(response.error);
        return out;
    }

private:
    servers::jellyfin::CurlTransport inner_;
};

} // namespace

std::unique_ptr<ITransport> makeCurlTransport(std::chrono::milliseconds connectTimeout,
                                              std::chrono::milliseconds totalTimeout) {
    return std::make_unique<CurlAdapter>(connectTimeout, totalTimeout);
}

} // namespace koalaamp::scrobble
