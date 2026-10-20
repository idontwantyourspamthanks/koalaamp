// SPDX-License-Identifier: MIT
// jellyfin — direct-vs-transcode decision. See JellyfinPlayback.hpp.

#include "servers/jellyfin/JellyfinPlayback.hpp"

#include "servers/jellyfin/JellyfinMediaSource.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

namespace koalaamp::servers::jellyfin {
namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

const char* playMethodName(PlayMethod method) {
    return method == PlayMethod::Transcode ? "Transcode" : "DirectPlay";
}

bool PlaybackPolicy::canDirectPlay(std::string_view codec, std::int64_t bitrateBps) const {
    if (bitrateBps > 0 && maxDirectBitrate > 0 && bitrateBps > maxDirectBitrate) {
        return false;
    }
    if (codec.empty()) {
        return true; // unknown metadata: do not transcode blindly
    }
    // `Container` may be a comma-separated list; direct play when it names one
    // of our codecs (Jellyfin can serve the original file for any of them).
    std::string current;
    const std::string text = lower(codec);
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == ',') {
            const auto first = current.find_first_not_of(" .");
            const auto last = current.find_last_not_of(" .");
            if (first != std::string::npos) {
                const std::string one = current.substr(first, last - first + 1);
                if (std::find(directCodecs.begin(), directCodecs.end(), one) !=
                    directCodecs.end()) {
                    return true;
                }
            }
            current.clear();
        } else {
            current.push_back(text[i]);
        }
    }
    return false;
}

JellyfinPlayback::JellyfinPlayback(IHttpTransport& transport, DeviceProfile profile,
                                   PlaybackPolicy policy)
    : transport_(transport), profile_(std::move(profile)), policy_(std::move(policy)) {}

PlaybackPlan JellyfinPlayback::plan(const std::string& baseUrl, const std::string& token,
                                    const std::string& deviceId, const library::Track& track,
                                    const std::string& playSessionId) const {
    PlaybackPlan plan;
    plan.itemId = track.id;
    plan.method = policy_.canDirectPlay(track.codec, track.bitrate) ? PlayMethod::DirectPlay
                                                                    : PlayMethod::Transcode;
    plan.spec.itemId = track.id;
    plan.spec.directPlay = plan.method == PlayMethod::DirectPlay;
    if (!plan.spec.directPlay) {
        plan.spec.audioCodec = policy_.transcodeCodec;
        plan.spec.audioBitRate = policy_.transcodeBitRate;
        plan.spec.maxAudioChannels = policy_.transcodeChannels;
        plan.spec.playSessionId = playSessionId;
    }

    plan.media.kind = playback::MediaRef::Kind::Stream;
    plan.media.value = track.title.empty() ? track.id : track.title;
    plan.media.stream = std::make_shared<JellyfinMediaSource>(
        transport_, profile_, baseUrl, token, deviceId, plan.spec);
    plan.media.rg = track.rg;
    plan.media.server = playback::ServerMediaInfo{track.id, {}, playMethodName(plan.method)};
    return plan;
}

} // namespace koalaamp::servers::jellyfin
