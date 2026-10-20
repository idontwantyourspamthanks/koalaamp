// SPDX-License-Identifier: MIT
// jellyfin — a read-only byte stream over an item's static audio.
//
// The wire contract is spec §8.2's: `GET /Audio/{id}/stream?static=true` with the
// MediaBrowser header (the token is never a URL parameter), Range-capable so seeking
// re-requests instead of downloading the whole track first.
//
// This is a `std::streambuf` on purpose: `ITrackSource::openStream` wants a
// `std::istream`, and SV-2's libvlc path (`libvlc_media_new_callbacks`) wants
// read/seek primitives — one implementation serves both. Note that `IHttpTransport`
// currently returns a whole body per request, so a seek costs one round-trip; the
// callback path's streaming transport is a separate extension.

#pragma once

#include "servers/jellyfin/AuthHeaders.hpp"
#include "servers/jellyfin/HttpTransport.hpp"

#include <cstdint>
#include <istream>
#include <memory>
#include <streambuf>
#include <string>

namespace koalaamp::servers::jellyfin {

/// Which form of `/Audio/{id}/stream` to request.
///
/// Direct play is the static file. Transcode asks the same endpoint for a
/// re-encoded progressive stream: libvlc cannot attach our Authorization header
/// to the HLS playlist's child requests, so an HLS fallback would force the
/// token into URLs — the one thing this client never does. The controller's
/// progressive form keeps header auth and still gets Range-style seeks.
struct StreamSpec {
    std::string itemId;
    bool directPlay{true};
    std::string audioCodec{"aac"}; ///< transcode target
    int audioBitRate{192000};      ///< bits/s
    int maxAudioChannels{2};
    std::string playSessionId;     ///< echoed to the server for transcode sessions
};

class JellyfinStream final : public std::streambuf {
public:
    /// Performs the first request at `startOffset`. Failure is not thrown: `ok()`
    /// is false and `error()` says why (401/404/transport).
    JellyfinStream(IHttpTransport& transport, DeviceProfile profile, std::string baseUrl,
                   std::string token, std::string deviceId, StreamSpec spec,
                   std::int64_t startOffset = 0);

    /// Byte offset of the next read within the resource.
    std::int64_t position() const { return start_ + static_cast<std::int64_t>(cursor_); }
    /// Resource size from `Content-Range`/`Content-Length`, or -1 when unknown.
    std::int64_t size() const { return size_; }
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }

protected:
    int_type underflow() override;
    pos_type seekoff(off_type offset, std::ios_base::seekdir dir,
                     std::ios_base::openmode which) override;
    pos_type seekpos(pos_type position, std::ios_base::openmode which) override;

private:
    /// Fetch from `offset`, replacing the buffer. Returns false (and sets `error_`)
    /// on any failure.
    bool fetch(std::int64_t offset);
    bool seekTo(std::int64_t offset);

    IHttpTransport& transport_;
    DeviceProfile profile_;
    std::string baseUrl_;
    std::string token_;
    std::string deviceId_;
    StreamSpec spec_;

    std::string body_;     ///< the current response's bytes
    std::int64_t start_ = 0; ///< resource offset of body_[0]
    std::size_t cursor_ = 0; ///< index into body_
    std::int64_t size_ = -1;
    bool ok_ = false;
    std::string error_;
};

/// `std::istream` ownership helper for the `ITrackSource` seam.
std::unique_ptr<std::istream> openJellyfinStream(IHttpTransport& transport, DeviceProfile profile,
                                                 const std::string& baseUrl,
                                                 const std::string& token,
                                                 const std::string& deviceId, StreamSpec spec,
                                                 std::string* error = nullptr);

} // namespace koalaamp::servers::jellyfin
