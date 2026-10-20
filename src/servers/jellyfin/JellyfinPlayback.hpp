// SPDX-License-Identifier: MIT
// jellyfin — direct-vs-transcode decision and the media reference it produces.
//
// Direct play is the static file (`?static=true`). When the codec is outside our
// decode set, or the source bitrate is above the link budget, we ask the server
// to transcode. The transcode request uses the AudioController's progressive
// form (`?static=false&audioCodec=…`) rather than HLS: libvlc's HTTP access has
// no way to attach our Authorization header to the playlist's child requests,
// so HLS would force the token into URLs. Both forms therefore travel through
// `libvlc_media_new_callbacks` with the token in the MediaBrowser header.
//
// PlayMethod is reported to `/Sessions/Playing*`; it must match what the server
// actually does or server-side scrobble plugins mis-account the play.

#pragma once

#include "playback/IAudioEngine.hpp"
#include "servers/jellyfin/AuthHeaders.hpp"
#include "servers/jellyfin/HttpTransport.hpp"
#include "servers/jellyfin/JellyfinStream.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace koalaamp::servers::jellyfin {

enum class PlayMethod { DirectPlay, Transcode };

/// The wire value (`DirectPlay` | `Transcode`) for reports.
const char* playMethodName(PlayMethod method);

struct PlaybackPolicy {
    /// Containers libvlc decodes for us; anything else is transcoded.
    std::vector<std::string> directCodecs{
        "flac", "alac", "mp3", "aac", "m4a", "mp4", "ogg", "oga",
        "opus", "wav", "ape", "wv",  "wma", "tta", "aiff", "aif"};
    /// Refuse direct play above this source bitrate (bits/s; 0 disables the cap).
    std::int64_t maxDirectBitrate{2'500'000};
    std::string transcodeCodec{"aac"};
    int transcodeBitRate{192'000};
    int transcodeChannels{2};

    /// Empty codec means "unknown": direct play (metadata absence must not
    /// force a transcode). Matching is case-insensitive.
    bool canDirectPlay(std::string_view codec, std::int64_t bitrateBps) const;
};

struct PlaybackPlan {
    playback::MediaRef media; ///< Kind::Stream; carries rg + server identity
    PlayMethod method{PlayMethod::DirectPlay};
    StreamSpec spec; ///< the exact request, for tests/diagnostics
    std::string itemId;
};

/// Turns a library `Track` (as mapped by `JellyfinClient::search`) into a media
/// reference the engine can play through its callback path.
class JellyfinPlayback {
public:
    JellyfinPlayback(IHttpTransport& transport, DeviceProfile profile, PlaybackPolicy policy = {});

    /// `playSessionId` is echoed on a transcode URL. The returned media's
    /// `server` field always names the item and the method to report.
    PlaybackPlan plan(const std::string& baseUrl, const std::string& token,
                      const std::string& deviceId, const library::Track& track,
                      const std::string& playSessionId = {}) const;

    const PlaybackPolicy& policy() const { return policy_; }

private:
    IHttpTransport& transport_;
    DeviceProfile profile_;
    PlaybackPolicy policy_;
};

} // namespace koalaamp::servers::jellyfin
