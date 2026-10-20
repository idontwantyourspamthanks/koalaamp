// SPDX-License-Identifier: MIT
// plex — Range stream. See PlexStream.hpp.

#include "servers/plex/PlexStream.hpp"

#include <charconv>
#include <utility>

namespace koalaamp::servers::plex {
namespace {

std::int64_t numberAt(std::string_view text) {
    std::int64_t value = -1;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} ? value : -1;
}

std::int64_t startFromContentRange(const std::string& value) {
    const auto space = value.find(' ');
    if (space == std::string::npos) {
        return -1;
    }
    const std::string_view rest(value.data() + space + 1, value.size() - space - 1);
    const auto dash = rest.find('-');
    return dash == std::string_view::npos ? -1 : numberAt(rest.substr(0, dash));
}

std::int64_t totalFromContentRange(const std::string& value) {
    const auto slash = value.rfind('/');
    if (slash == std::string::npos) {
        return -1;
    }
    const std::string_view tail(value.data() + slash + 1, value.size() - slash - 1);
    if (tail == "*") {
        return -1;
    }
    return numberAt(tail);
}

} // namespace

PlexStream::PlexStream(jellyfin::IHttpTransport& transport, std::string url,
                       std::vector<std::pair<std::string, std::string>> headers,
                       std::int64_t startOffset)
    : transport_(transport), url_(std::move(url)), headers_(std::move(headers)) {
    ok_ = fetch(startOffset);
}

bool PlexStream::fetch(std::int64_t offset) {
    body_.clear();
    start_ = offset;
    cursor_ = 0;

    jellyfin::HttpRequest request;
    request.method = "GET";
    request.url = url_;
    request.headers = headers_;
    if (offset > 0) {
        request.headers.emplace_back("Range", "bytes=" + std::to_string(offset) + "-");
    }

    const jellyfin::HttpResponse response = transport_.perform(request, jellyfin::NoCancellation{});
    if (!response.transportOk()) {
        error_ = response.error;
        return false;
    }
    if (response.status == 401) {
        error_ = "The Plex server rejected the token";
        return false;
    }
    if (response.status == 404) {
        error_ = "Media part not found";
        return false;
    }
    if (!response.httpOk()) {
        error_ = "HTTP " + std::to_string(response.status);
        return false;
    }

    body_ = response.body;
    if (const std::string* range = response.header("Content-Range"); range != nullptr) {
        if (const std::int64_t realStart = startFromContentRange(*range); realStart >= 0) {
            start_ = realStart;
        }
        size_ = totalFromContentRange(*range);
    } else if (const std::string* length = response.header("Content-Length");
               length != nullptr) {
        size_ = numberAt(*length);
    }
    setg(body_.data(), body_.data(), body_.data() + body_.size());
    return true;
}

PlexStream::int_type PlexStream::underflow() {
    if (gptr() != nullptr && gptr() < egptr()) {
        return traits_type::to_int_type(*gptr());
    }
    return traits_type::eof();
}

bool PlexStream::seekTo(std::int64_t offset) {
    const std::int64_t begin = start_;
    const std::int64_t end = begin + static_cast<std::int64_t>(body_.size());
    if (offset >= begin && offset <= end) {
        setg(body_.data(), body_.data() + (offset - begin), body_.data() + body_.size());
        return true;
    }
    return fetch(offset);
}

PlexStream::pos_type PlexStream::seekoff(off_type offset, std::ios_base::seekdir dir,
                                         std::ios_base::openmode which) {
    if ((which & std::ios_base::in) == 0) {
        return pos_type(off_type(-1));
    }
    const std::int64_t current =
        start_ + static_cast<std::int64_t>(gptr() != nullptr ? gptr() - eback() : 0);
    std::int64_t target = 0;
    switch (dir) {
        case std::ios_base::beg:
            target = offset;
            break;
        case std::ios_base::cur:
            target = current + offset;
            break;
        case std::ios_base::end:
            if (size_ < 0) {
                return pos_type(off_type(-1));
            }
            target = size_ + offset;
            break;
        default:
            return pos_type(off_type(-1));
    }
    if (target < 0 || !seekTo(target)) {
        return pos_type(off_type(-1));
    }
    return pos_type(static_cast<off_type>(target));
}

PlexStream::pos_type PlexStream::seekpos(pos_type position, std::ios_base::openmode which) {
    return seekoff(static_cast<off_type>(position), std::ios_base::beg, which);
}

std::unique_ptr<std::istream> openPlexStream(
    jellyfin::IHttpTransport& transport, std::string url,
    std::vector<std::pair<std::string, std::string>> headers, std::string* error) {
    auto buffer = std::make_unique<PlexStream>(transport, std::move(url), std::move(headers));
    if (!buffer->ok()) {
        if (error != nullptr) {
            *error = buffer->error();
        }
        return nullptr;
    }
    return std::make_unique<std::istream>(buffer.release());
}

} // namespace koalaamp::servers::plex
