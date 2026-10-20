// SPDX-License-Identifier: MIT
// jellyfin — remote-control adapter onto the real queue/transport (SV-2).
//
// `JellyfinRemoteClient` speaks the protocol; this class performs its commands.
// The important mapping is `PlayNext` -> `PlaybackCore::enqueueNext` (the next
// track, without hijacking the current one) and `PlayLast` -> `enqueue`.

#pragma once

#include "playback/PlaybackCore.hpp"
#include "servers/jellyfin/JellyfinRemote.hpp"

#include <functional>
#include <optional>

namespace koalaamp::servers::jellyfin {

class PlaybackRemoteAdapter final : public IRemotePlayback {
public:
    /// Turns a server item reference into a playable media. Returning nullopt
    /// (unknown item, no session) drops that item, not the whole command.
    using Resolver =
        std::function<std::optional<koalaamp::playback::MediaRef>(const RemoteTrackRef&)>;

    PlaybackRemoteAdapter(koalaamp::playback::PlaybackCore& core, Resolver resolver);

    void playNow(const std::vector<RemoteTrackRef>& items) override;
    void playNext(const std::vector<RemoteTrackRef>& items) override;
    void playLast(const std::vector<RemoteTrackRef>& items) override;
    void stop() override;
    void pause() override;
    void unpause() override;
    void nextTrack() override;
    void previousTrack() override;
    void seekMs(std::int64_t positionMs) override;
    void setVolume(int volume100) override;
    void playPause() override;

private:
    std::vector<koalaamp::playback::MediaRef> resolve(const std::vector<RemoteTrackRef>& items);

    koalaamp::playback::PlaybackCore& core_;
    Resolver resolver_;
};

} // namespace koalaamp::servers::jellyfin
