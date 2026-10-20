// SPDX-License-Identifier: MIT
// jellyfin — remote-control adapter onto PlaybackCore. See the header.

#include "servers/jellyfin/JellyfinRemotePlayback.hpp"

#include <utility>

namespace koalaamp::servers::jellyfin {

PlaybackRemoteAdapter::PlaybackRemoteAdapter(koalaamp::playback::PlaybackCore& core,
                                             Resolver resolver)
    : core_(core), resolver_(std::move(resolver)) {}

std::vector<koalaamp::playback::MediaRef>
PlaybackRemoteAdapter::resolve(const std::vector<RemoteTrackRef>& items) {
    std::vector<koalaamp::playback::MediaRef> media;
    for (const RemoteTrackRef& item : items) {
        if (!resolver_) {
            break;
        }
        if (std::optional<koalaamp::playback::MediaRef> resolved = resolver_(item)) {
            media.push_back(std::move(*resolved));
        }
    }
    return media;
}

void PlaybackRemoteAdapter::playNow(const std::vector<RemoteTrackRef>& items) {
    std::vector<koalaamp::playback::MediaRef> media = resolve(items);
    if (media.empty()) {
        return;
    }
    // PlayNow replaces what is queued; the rest of the cast becomes the queue.
    core_.clearQueue();
    core_.play(media.front());
    for (std::size_t i = 1; i < media.size(); ++i) {
        core_.enqueue(std::move(media[i]));
    }
}

void PlaybackRemoteAdapter::playNext(const std::vector<RemoteTrackRef>& items) {
    for (auto& media : resolve(items)) {
        core_.enqueueNext(std::move(media)); // next up; current track untouched
    }
}

void PlaybackRemoteAdapter::playLast(const std::vector<RemoteTrackRef>& items) {
    for (auto& media : resolve(items)) {
        core_.enqueue(std::move(media));
    }
}

void PlaybackRemoteAdapter::stop() {
    core_.stop();
}

void PlaybackRemoteAdapter::pause() {
    core_.pause();
}

void PlaybackRemoteAdapter::unpause() {
    core_.resume();
}

void PlaybackRemoteAdapter::nextTrack() {
    core_.next();
}

void PlaybackRemoteAdapter::previousTrack() {
    core_.previous();
}

void PlaybackRemoteAdapter::seekMs(std::int64_t positionMs) {
    core_.seekMs(positionMs);
}

void PlaybackRemoteAdapter::setVolume(int volume100) {
    core_.setVolume100(volume100);
}

void PlaybackRemoteAdapter::playPause() {
    core_.togglePause();
}

} // namespace koalaamp::servers::jellyfin
