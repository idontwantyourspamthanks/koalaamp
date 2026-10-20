// SPDX-License-Identifier: MIT
// plex — playback source. See PlexMediaSource.hpp.

#include "servers/plex/PlexMediaSource.hpp"

#include "servers/plex/PlexStream.hpp"

#include <istream>
#include <utility>

namespace koalaamp::servers::plex {
namespace {

class PlexMediaStream final : public koalaamp::playback::IMediaStream {
public:
    explicit PlexMediaStream(std::unique_ptr<PlexStream> buffer)
        : in_(std::make_unique<std::istream>(buffer.release())) {}

    std::ptrdiff_t read(unsigned char* buf, std::size_t len) override {
        in_->read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(len));
        const std::streamsize got = in_->gcount();
        if (in_->bad() || (got == 0 && in_->fail() && !in_->eof())) {
            return -1;
        }
        return static_cast<std::ptrdiff_t>(got);
    }

    bool seek(std::int64_t offset) override {
        in_->clear();
        in_->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        return in_->good();
    }

private:
    std::unique_ptr<std::istream> in_;
};

std::string baseNoSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

} // namespace

PlexMediaSource::PlexMediaSource(jellyfin::IHttpTransport& transport, PlexClientInfo info,
                                 std::string baseUrl, std::string token, std::string partPath)
    : transport_(transport),
      info_(std::move(info)),
      baseUrl_(baseNoSlash(std::move(baseUrl))),
      token_(std::move(token)),
      partPath_(std::move(partPath)) {}

std::unique_ptr<koalaamp::playback::IMediaStream> PlexMediaSource::open(std::int64_t* size) {
    if (partPath_.empty()) {
        lastError_ = "Track has no media part";
        return nullptr;
    }
    auto buffer = std::make_unique<PlexStream>(transport_, baseUrl_ + partPath_,
                                               plexHeaders(info_, token_));
    if (!buffer->ok()) {
        lastError_ = buffer->error();
        return nullptr;
    }
    if (size != nullptr) {
        *size = buffer->size();
    }
    lastError_.clear();
    return std::make_unique<PlexMediaStream>(std::move(buffer));
}

} // namespace koalaamp::servers::plex
