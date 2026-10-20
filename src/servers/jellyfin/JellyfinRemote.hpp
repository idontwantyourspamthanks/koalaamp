// SPDX-License-Identifier: MIT
// jellyfin — `/socket` remote control (SV-2).
//
// The server pushes `{MessageType, Data}` text frames. This layer parses the
// catalog we implement — `Play` (PlayNow/PlayNext/PlayLast), `Playstate`
// (Stop/Pause/Unpause/NextTrack/PreviousTrack/Seek), `GeneralCommand`
// (SetVolume/Seek/PlayPause), `ForceKeepAlive`, `ServerRestarting` — and routes
// them onto the app's queue/transport through `IRemotePlayback`.
//
// "Play next" enqueues, it does not hijack the current track: the interface
// separates `playNow` from `playNext`/`playLast` so the adapter cannot confuse
// the two.

#pragma once

#include "servers/jellyfin/WebSocket.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace koalaamp::servers::jellyfin {

struct RemoteTrackRef {
    std::string id;
    std::string name;
};

enum class RemoteCommandKind {
    PlayNow,
    PlayNext,
    PlayLast,
    Stop,
    Pause,
    Unpause,
    NextTrack,
    PreviousTrack,
    Seek,
    SetVolume,
    PlayPause,
};

struct RemoteCommand {
    RemoteCommandKind kind{RemoteCommandKind::PlayNow};
    std::vector<RemoteTrackRef> items;
    std::int64_t positionMs{0};
    int volume100{-1};
};

enum class RemoteMessageKind { Command, KeepAlive, ForceKeepAlive, ServerRestarting, Other };

struct RemoteMessage {
    RemoteMessageKind kind{RemoteMessageKind::Other};
    RemoteCommand command;
};

/// Parses one server frame. Returns nullopt on malformed JSON.
std::optional<RemoteMessage> parseRemoteMessage(std::string_view text);

/// The app's remote-control surface. `playNext` maps to enqueue-next and
/// `playLast` to append, so a cast never replaces the current track.
class IRemotePlayback {
public:
    virtual ~IRemotePlayback() = default;
    virtual void playNow(const std::vector<RemoteTrackRef>& items) = 0;
    virtual void playNext(const std::vector<RemoteTrackRef>& items) = 0;
    virtual void playLast(const std::vector<RemoteTrackRef>& items) = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void unpause() = 0;
    virtual void nextTrack() = 0;
    virtual void previousTrack() = 0;
    virtual void seekMs(std::int64_t positionMs) = 0;
    virtual void setVolume(int volume100) = 0;
    virtual void playPause() = 0;
};

/// Routes one command. Returns false for a no-op (empty play list).
bool routeRemoteCommand(const RemoteCommand& command, IRemotePlayback& playback);

enum class RemoteCloseReason { Closed, ServerRestarting, Unauthorized };

class JellyfinRemoteClient {
public:
    JellyfinRemoteClient(IWebSocketTransport& transport, IRemotePlayback& playback);

    /// One connection: reads until the peer closes, `stop` is set, or the server
    /// announces a restart. `ForceKeepAlive` frames are answered with
    /// `{"MessageType":"KeepAlive"}`. Reconnect by calling `run` again after
    /// `backoffFor(attempt)`.
    RemoteCloseReason run(std::atomic<bool>& stop, std::string* error = nullptr);

    /// 1 s, 2 s, 4 s … capped at 30 s.
    static std::chrono::milliseconds backoffFor(int attempt);

private:
    IWebSocketTransport& transport_;
    IRemotePlayback& playback_;
};

} // namespace koalaamp::servers::jellyfin
