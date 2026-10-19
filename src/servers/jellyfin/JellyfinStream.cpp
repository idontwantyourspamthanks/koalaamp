// SPDX-License-Identifier: MIT
// jellyfin — read-only Range-capable audio stream. See JellyfinStream.hpp.

#include "servers/jellyfin/JellyfinStream.hpp"

#include <charconv>
#include <utility>

namespace koalaamp::servers::jellyfin {
namespace {

std::string baseUrlNoSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

std::int64_t numberAt(std::string_view text) {
    std::int64_t value = -1;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc{} ? value : -1;
}

/// "bytes 100-199/500" → the total (500), or -1 for `*` and malformed values.
std::int64_t totalFromContentRange(const std::string& value) {
    const auto slash = value.rfind('/');
    if (slash == std::string::npos) {
        return -1;
    }
    const std::string_view tail(value.data() + slash + 1, value.size() - slash - 1);
    return tail == "*" ? -1 : numberAt(tail);
}

/// "bytes 100-199/500" → 100; absent/malformed → -1.
std::int64_t startFromContentRange(const std::string& value) {
    const auto space = value.find(' ');
    if (space == std::string::npos) {
        return -1;
    }
    const std::string_view rest(value.data() + space + 1, value.size() - space - 1);
    const auto dash = rest.find('-');
    if (dash == std::string_view::npos) {
        return -1;
    }
    return numberAt(rest.substr(0, dash));
}

} // namespace

JellyfinStream::JellyfinStream(IHttpTransport& transport, DeviceProfile profile,
                               std::string baseUrl, std::string token, std::string deviceId,
                               std::string itemId, std::int64_t startOffset)
    : transport_(transport),
      profile_(std::move(profile)),
      baseUrl_(std::move(baseUrl)),
      token_(std::move(token)),
      deviceId_(std::move(deviceId)),
      itemId_(std::move(itemId)) {
    ok_ = fetch(startOffset);
}

bool JellyfinStream::fetch(std::int64_t offset) {
    body_.clear();
    start_ = offset;

    HttpRequest request;
    request.method = "GET";
    request.url = baseUrlNoSlash(baseUrl_) + "/Audio/" + itemId_ + "/stream?static=true";
    request.headers = baseHeaders(profile_, deviceId_, token_);
    if (offset > 0) {
        request.headers.emplace_back("Range", "bytes=" + std::to_string(offset) + "-");
    }

    const HttpResponse response = transport_.perform(request, NoCancellation{});
    if (!response.transportOk()) {
        error_ = response.error;
        return false;
    }
    if (response.status == 401) {
        error_ = "The server rejected the token";
        return false;
    }
    if (response.status == 404) {
        error_ = "Item not found";
        return false;
    }
    if (!response.httpOk()) {
        error_ = "Server responded with HTTP " + std::to_string(response.status);
        return false;
    }

    body_ = response.body;
    if (const std::string* range = response.header("Content-Range"); range != nullptr) {
        const std::int64_t realStart = startFromContentRange(*range);
        if (realStart >= 0) {
            start_ = realStart;
        }
        size_ = totalFromContentRange(*range);
    } else if (const std::string* length = response.header("Content-Length"); length != nullptr) {
        size_ = numberAt(*length);
    }
    setg(body_.data(), body_.data(), body_.data() + body_.size());
    return true;
}

JellyfinStream::int_type JellyfinStream::underflow() {
    if (gptr() != nullptr && gptr() < egptr()) {
        return traits_type::to_int_type(*gptr());
    }
    return traits_type::eof();
}

bool JellyfinStream::seekTo(std::int64_t offset) {
    // A seek inside the buffered response just moves the get pointer; anything else
    // re-requests with a Range header.
    const std::int64_t begin = start_;
    const std::int64_t end = begin + static_cast<std::int64_t>(body_.size());
    if (offset >= begin && offset <= end) {
        setg(body_.data(), body_.data() + (offset - begin), body_.data() + body_.size());
        return true;
    }
    return fetch(offset);
}

JellyfinStream::pos_type JellyfinStream::seekoff(off_type offset, std::ios_base::seekdir dir,
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

JellyfinStream::pos_type JellyfinStream::seekpos(pos_type position, std::ios_base::openmode which) {
    return seekoff(static_cast<off_type>(position), std::ios_base::beg, which);
}

std::unique_ptr<std::istream> openJellyfinStream(IHttpTransport& transport, DeviceProfile profile,
                                                 const std::string& baseUrl,
                                                 const std::string& token,
                                                 const std::string& deviceId,
                                                 const std::string& itemId, std::string* error) {
    auto buffer = std::make_unique<JellyfinStream>(transport, std::move(profile), baseUrl, token,
                                                   deviceId, itemId);
    if (!buffer->ok()) {
        if (error != nullptr) {
            *error = buffer->error();
        }
        return nullptr;
    }
    return std::make_unique<std::istream>(buffer.release());
}

} // namespace koalaamp::servers::jellyfin
