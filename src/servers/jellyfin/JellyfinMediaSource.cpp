// SPDX-License-Identifier: MIT
// jellyfin — callback-stream adapter for libvlc. See JellyfinMediaSource.hpp.

#include "servers/jellyfin/JellyfinMediaSource.hpp"

#include "servers/jellyfin/JellyfinStream.hpp"

#include <istream>
#include <utility>

namespace koalaamp::servers::jellyfin {
namespace {

/// One open stream, read through `std::istream` so gcount/state semantics are
/// the standard library's rather than ours. The `std::istream` owns the
/// `JellyfinStream` buffer and deletes it on destruction.
class JellyfinMediaStream final : public koalaamp::playback::IMediaStream {
public:
    explicit JellyfinMediaStream(std::unique_ptr<JellyfinStream> buffer)
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
        // A failed read leaves failbit set; clear before seeking so the stream
        // can recover (libvlc seeks after probing headers).
        in_->clear();
        in_->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        return in_->good();
    }

private:
    std::unique_ptr<std::istream> in_;
};

} // namespace

JellyfinMediaSource::JellyfinMediaSource(IHttpTransport& transport, DeviceProfile profile,
                                         std::string baseUrl, std::string token,
                                         std::string deviceId, StreamSpec spec)
    : transport_(transport),
      profile_(std::move(profile)),
      baseUrl_(std::move(baseUrl)),
      token_(std::move(token)),
      deviceId_(std::move(deviceId)),
      spec_(std::move(spec)) {}

std::unique_ptr<koalaamp::playback::IMediaStream> JellyfinMediaSource::open(std::int64_t* size) {
    auto buffer = std::make_unique<JellyfinStream>(transport_, profile_, baseUrl_, token_,
                                                   deviceId_, spec_);
    if (!buffer->ok()) {
        lastError_ = buffer->error();
        return nullptr;
    }
    if (size != nullptr) {
        *size = buffer->size();
    }
    lastError_.clear();
    return std::make_unique<JellyfinMediaStream>(std::move(buffer));
}

} // namespace koalaamp::servers::jellyfin
