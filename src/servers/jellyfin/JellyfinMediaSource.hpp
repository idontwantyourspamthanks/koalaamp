// SPDX-License-Identifier: MIT
// jellyfin — the playback-side face of JellyfinStream.
//
// SV-2 direct play hands libvlc a byte source instead of a URL: the token stays
// in the MediaBrowser header, and a seek becomes a Range re-request rather than
// a credential-bearing locator. `JellyfinMediaSource` is the
// `IMediaStreamSource` that the tap's `libvlc_media_new_callbacks` drives.

#pragma once

#include "playback/MediaStream.hpp"
#include "servers/jellyfin/AuthHeaders.hpp"
#include "servers/jellyfin/HttpTransport.hpp"

#include <memory>
#include <string>

namespace koalaamp::servers::jellyfin {

class JellyfinMediaSource final : public koalaamp::playback::IMediaStreamSource {
public:
    /// `transport` MUST outlive the source and every stream it opens.
    JellyfinMediaSource(IHttpTransport& transport, DeviceProfile profile, std::string baseUrl,
                        std::string token, std::string deviceId, std::string itemId);

    /// Opens `/Audio/{id}/stream?static=true` (one fresh request per call). On
    /// failure returns nullptr and `lastError()` carries the reason.
    std::unique_ptr<koalaamp::playback::IMediaStream> open(std::int64_t* size) override;

    const std::string& lastError() const { return lastError_; }

private:
    IHttpTransport& transport_;
    DeviceProfile profile_;
    std::string baseUrl_;
    std::string token_;
    std::string deviceId_;
    std::string itemId_;
    std::string lastError_;
};

} // namespace koalaamp::servers::jellyfin
