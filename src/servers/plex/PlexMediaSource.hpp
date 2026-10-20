// SPDX-License-Identifier: MIT
// plex — playback source for libvlc's callback path (SV-4).
//
// Wraps the Plex Range stream in the engine's `IMediaStreamSource` seam, so
// `libvlc_media_new_callbacks` can play a PMS part without temp files. The token
// travels in X-Plex-Token on every request.

#pragma once

#include "playback/IAudioEngine.hpp"
#include "playback/MediaStream.hpp"
#include "servers/jellyfin/HttpTransport.hpp"
#include "servers/plex/PlexTypes.hpp"

#include <memory>
#include <string>

namespace koalaamp::servers::plex {

class PlexMediaSource final : public koalaamp::playback::IMediaStreamSource {
public:
    PlexMediaSource(jellyfin::IHttpTransport& transport, PlexClientInfo info, std::string baseUrl,
                    std::string token, std::string partPath);

    std::unique_ptr<koalaamp::playback::IMediaStream> open(std::int64_t* size) override;

    const std::string& lastError() const { return lastError_; }

private:
    jellyfin::IHttpTransport& transport_;
    PlexClientInfo info_;
    std::string baseUrl_;
    std::string token_;
    std::string partPath_;
    std::string lastError_;
};

/// Builds the engine reference for a PMS track: Kind::Stream over the part URL,
/// with the server identity the timeline reporter consumes.
inline koalaamp::playback::MediaRef makePlexMedia(jellyfin::IHttpTransport& transport,
                                                  PlexClientInfo info, const std::string& baseUrl,
                                                  const std::string& token,
                                                  const library::Track& track) {
    koalaamp::playback::MediaRef media;
    media.kind = koalaamp::playback::MediaRef::Kind::Stream;
    media.value = track.title;
    media.stream =
        std::make_shared<PlexMediaSource>(transport, std::move(info), baseUrl, token, track.path);
    media.server = koalaamp::playback::ServerMediaInfo{track.id, {}, "DirectPlay"};
    media.track = track;
    return media;
}

} // namespace koalaamp::servers::plex
