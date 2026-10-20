// SPDX-License-Identifier: MIT
// jellyfin — `/socket` message catalog and remote-control loop. See the header.

#include "servers/jellyfin/JellyfinRemote.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <utility>

namespace koalaamp::servers::jellyfin {
namespace {

using json = nlohmann::json;

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return lower(a) == lower(b);
}

std::int64_t ticksToMs(std::int64_t ticks) {
    return ticks / 10000;
}

std::vector<RemoteTrackRef> itemsFrom(const json& data) {
    std::vector<RemoteTrackRef> items;
    const auto found = data.find("ItemIds");
    if (found == data.end() || !found->is_array()) {
        return items;
    }
    for (const auto& id : *found) {
        if (id.is_string()) {
            items.push_back(RemoteTrackRef{id.get<std::string>(), {}});
        }
    }
    if (const auto names = data.find("ItemNames");
        names != data.end() && names->is_array()) {
        std::size_t index = 0;
        for (const auto& name : *names) {
            if (index < items.size() && name.is_string()) {
                items[index].name = name.get<std::string>();
            }
            ++index;
        }
    }
    return items;
}

std::optional<std::int64_t> numberArgument(const json& data, const char* key) {
    const auto arguments = data.find("Arguments");
    if (arguments == data.end() || !arguments->is_object()) {
        return std::nullopt;
    }
    const auto value = arguments->find(key);
    if (value == arguments->end()) {
        return std::nullopt;
    }
    if (value->is_number_integer()) {
        return value->get<std::int64_t>();
    }
    if (value->is_string()) {
        try {
            return std::stoll(value->get<std::string>());
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<RemoteMessage> parseRemoteMessage(std::string_view text) {
    const json root = json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return std::nullopt;
    }
    const std::string type = lower(root.value("MessageType", std::string{}));
    RemoteMessage message;

    if (type == "forcekeepalive") {
        message.kind = RemoteMessageKind::ForceKeepAlive;
        return message;
    }
    if (type == "keepalive") {
        message.kind = RemoteMessageKind::KeepAlive;
        return message;
    }
    if (type == "serverrestarting") {
        message.kind = RemoteMessageKind::ServerRestarting;
        return message;
    }
    if (type == "play") {
        const json data = root.value("Data", json::object());
        message.kind = RemoteMessageKind::Command;
        const std::string playCommand = lower(data.value("PlayCommand", std::string{}));
        if (playCommand == "playnext") {
            message.command.kind = RemoteCommandKind::PlayNext;
        } else if (playCommand == "playlast") {
            message.command.kind = RemoteCommandKind::PlayLast;
        } else {
            message.command.kind = RemoteCommandKind::PlayNow;
        }
        message.command.items = itemsFrom(data);
        message.command.positionMs =
            ticksToMs(data.value("StartPositionTicks", static_cast<std::int64_t>(0)));
        return message;
    }
    if (type == "playstate") {
        const json data = root.value("Data", json::object());
        message.kind = RemoteMessageKind::Command;
        const std::string command = lower(data.value("Command", std::string{}));
        if (command == "stop") {
            message.command.kind = RemoteCommandKind::Stop;
        } else if (command == "pause") {
            message.command.kind = RemoteCommandKind::Pause;
        } else if (command == "unpause") {
            message.command.kind = RemoteCommandKind::Unpause;
        } else if (command == "nexttrack" || command == "next") {
            message.command.kind = RemoteCommandKind::NextTrack;
        } else if (command == "previoustrack" || command == "previous") {
            message.command.kind = RemoteCommandKind::PreviousTrack;
        } else if (command == "seek") {
            message.command.kind = RemoteCommandKind::Seek;
        } else {
            message.kind = RemoteMessageKind::Other;
            return message;
        }
        message.command.positionMs =
            ticksToMs(data.value("SeekPositionTicks", static_cast<std::int64_t>(0)));
        return message;
    }
    if (type == "generalcommand") {
        const json data = root.value("Data", json::object());
        message.kind = RemoteMessageKind::Command;
        const std::string name = lower(data.value("Name", std::string{}));
        if (name == "setvolume") {
            message.command.kind = RemoteCommandKind::SetVolume;
            const auto volume = numberArgument(data, "Volume");
            message.command.volume100 = volume ? static_cast<int>(*volume) : -1;
        } else if (name == "seek") {
            message.command.kind = RemoteCommandKind::Seek;
            const auto ticks = numberArgument(data, "SeekPositionTicks");
            message.command.positionMs = ticks ? ticksToMs(*ticks) : 0;
        } else if (name == "playpause") {
            message.command.kind = RemoteCommandKind::PlayPause;
        } else {
            message.kind = RemoteMessageKind::Other;
        }
        return message;
    }
    if (type == "playall") {
        return message; // Other: playlist management is out of scope.
    }
    return message;
}

bool routeRemoteCommand(const RemoteCommand& command, IRemotePlayback& playback) {
    switch (command.kind) {
        case RemoteCommandKind::PlayNow:
            if (command.items.empty()) {
                return false;
            }
            playback.playNow(command.items);
            return true;
        case RemoteCommandKind::PlayNext:
            if (command.items.empty()) {
                return false;
            }
            playback.playNext(command.items);
            return true;
        case RemoteCommandKind::PlayLast:
            if (command.items.empty()) {
                return false;
            }
            playback.playLast(command.items);
            return true;
        case RemoteCommandKind::Stop:
            playback.stop();
            return true;
        case RemoteCommandKind::Pause:
            playback.pause();
            return true;
        case RemoteCommandKind::Unpause:
            playback.unpause();
            return true;
        case RemoteCommandKind::NextTrack:
            playback.nextTrack();
            return true;
        case RemoteCommandKind::PreviousTrack:
            playback.previousTrack();
            return true;
        case RemoteCommandKind::Seek:
            playback.seekMs(command.positionMs);
            return true;
        case RemoteCommandKind::SetVolume:
            if (command.volume100 < 0) {
                return false;
            }
            playback.setVolume(command.volume100);
            return true;
        case RemoteCommandKind::PlayPause:
            playback.playPause();
            return true;
    }
    return false;
}

JellyfinRemoteClient::JellyfinRemoteClient(IWebSocketTransport& transport,
                                           IRemotePlayback& playback)
    : transport_(transport), playback_(playback) {}

RemoteCloseReason JellyfinRemoteClient::run(std::atomic<bool>& stop, std::string* error) {
    if (!transport_.connect(error)) {
        return RemoteCloseReason::Closed;
    }
    while (!stop.load(std::memory_order_relaxed)) {
        std::string text;
        bool closed = false;
        if (!transport_.receive(&text, 1000, &closed, error)) {
            if (closed) {
                break;
            }
            continue; // idle timeout: keep waiting
        }
        const std::optional<RemoteMessage> message = parseRemoteMessage(text);
        if (!message) {
            if (error != nullptr) {
                *error = "Malformed server message";
            }
            continue;
        }
        switch (message->kind) {
            case RemoteMessageKind::ForceKeepAlive:
                if (!transport_.sendText("{\"MessageType\":\"KeepAlive\"}", error)) {
                    transport_.close();
                    return RemoteCloseReason::Closed;
                }
                break;
            case RemoteMessageKind::ServerRestarting:
                transport_.close();
                return RemoteCloseReason::ServerRestarting;
            case RemoteMessageKind::Command:
                routeRemoteCommand(message->command, playback_);
                break;
            case RemoteMessageKind::KeepAlive:
            case RemoteMessageKind::Other:
                break;
        }
    }
    transport_.close();
    return RemoteCloseReason::Closed;
}

std::chrono::milliseconds JellyfinRemoteClient::backoffFor(int attempt) {
    const int exponent = attempt < 0 ? 0 : std::min(attempt, 5);
    const std::int64_t delayMs = static_cast<std::int64_t>(1000) << exponent;
    return std::chrono::milliseconds(std::min<std::int64_t>(delayMs, 30000));
}

} // namespace koalaamp::servers::jellyfin
