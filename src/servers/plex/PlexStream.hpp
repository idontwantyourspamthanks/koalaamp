// SPDX-License-Identifier: MIT
// plex — Range-capable byte stream over a PMS part URL (SV-3 → SV-4).
//
// Same shape as the Jellyfin stream: a `std::streambuf` that re-requests with
// `Range: bytes=N-` when a seek leaves the buffered response. The token travels
// in the X-Plex-Token header, never in the URL.

#pragma once

#include "servers/jellyfin/HttpTransport.hpp"

#include <cstdint>
#include <istream>
#include <memory>
#include <streambuf>
#include <string>
#include <utility>
#include <vector>

namespace koalaamp::servers::plex {

class PlexStream final : public std::streambuf {
public:
    PlexStream(jellyfin::IHttpTransport& transport, std::string url,
               std::vector<std::pair<std::string, std::string>> headers,
               std::int64_t startOffset = 0);

    std::int64_t position() const { return start_ + static_cast<std::int64_t>(cursor_); }
    std::int64_t size() const { return size_; }
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

protected:
    int_type underflow() override;
    pos_type seekoff(off_type offset, std::ios_base::seekdir dir,
                     std::ios_base::openmode which) override;
    pos_type seekpos(pos_type position, std::ios_base::openmode which) override;

private:
    bool fetch(std::int64_t offset);
    bool seekTo(std::int64_t offset);

    jellyfin::IHttpTransport& transport_;
    std::string url_;
    std::vector<std::pair<std::string, std::string>> headers_;

    std::string body_;
    std::int64_t start_{0};
    std::size_t cursor_{0};
    std::int64_t size_{-1};
    bool ok_{false};
    std::string error_;
};

std::unique_ptr<std::istream> openPlexStream(
    jellyfin::IHttpTransport& transport, std::string url,
    std::vector<std::pair<std::string, std::string>> headers, std::string* error = nullptr);

} // namespace koalaamp::servers::plex
