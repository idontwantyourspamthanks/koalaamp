// SPDX-License-Identifier: MIT
#include "ui/PlaybackCoreTransport.hpp"

#include "ui/MediaName.hpp"

namespace koalaamp::ui {

PlaybackCoreTransport::PlaybackCoreTransport(playback::PlaybackCore& core, VisFeed& feed,
                                             QObject* parent)
    : PlaybackCoreTransport(core, feed, feed.callback(), parent) {}

PlaybackCoreTransport::PlaybackCoreTransport(playback::PlaybackCore& core, VisFeed& feed,
                                             playback::IAudioEngine::PcmCb chain, QObject* parent)
    : QObject(parent), m_core(core), m_feed(feed) {
    // IAudioEngine::setPcmCallback is the DSP insertion seam (audio thread).
    m_core.setPcmCallback(chain ? std::move(chain) : m_feed.callback());
    m_core.addObserver(this);
}

PlaybackCoreTransport::~PlaybackCoreTransport() {
    m_core.removeObserver(this);
    m_core.setPcmCallback(nullptr);
}

void PlaybackCoreTransport::playMedia(const playback::MediaRef& media) {
    m_core.play(media);
}

void PlaybackCoreTransport::resume() {
    m_core.resume();
}

void PlaybackCoreTransport::pause() {
    m_core.pause();
}

void PlaybackCoreTransport::togglePause() {
    m_core.togglePause();
}

void PlaybackCoreTransport::stop() {
    m_core.stop();
}

void PlaybackCoreTransport::next() {
    m_core.next();
}

void PlaybackCoreTransport::previous() {
    m_core.previous();
}

void PlaybackCoreTransport::seekMs(std::int64_t positionMs) {
    m_core.seekMs(positionMs);
}

void PlaybackCoreTransport::setVolumePercent(int volume) {
    m_core.setVolume100(volume);
}

void PlaybackCoreTransport::setShuffle(bool enabled) {
    m_core.setShuffle(enabled);
}

void PlaybackCoreTransport::setRepeatMode(playback::RepeatMode mode) {
    m_core.setRepeat(mode);
}

void PlaybackCoreTransport::enqueue(const playback::MediaRef& media) {
    m_core.enqueue(media);
}

void PlaybackCoreTransport::playList(const std::vector<playback::MediaRef>& list,
                                     std::size_t start) {
    m_core.playList(list, start);
}

void PlaybackCoreTransport::clearQueue() {
    m_core.clearQueue();
}

std::size_t PlaybackCoreTransport::queueSize() const {
    return m_core.queueSize();
}

std::optional<playback::MediaRef> PlaybackCoreTransport::peekNext() const {
    return m_core.peekNext();
}

bool PlaybackCoreTransport::hasMedia() const {
    return m_core.currentMedia().has_value();
}

playback::PlaybackState PlaybackCoreTransport::state() const {
    return m_core.state();
}

std::int64_t PlaybackCoreTransport::positionMs() const {
    return m_core.positionMs();
}

std::int64_t PlaybackCoreTransport::lengthMs() const {
    return m_core.lengthMs();
}

int PlaybackCoreTransport::volumePercent() const {
    return m_core.volume100();
}

bool PlaybackCoreTransport::shuffle() const {
    return m_core.shuffle();
}

playback::RepeatMode PlaybackCoreTransport::repeatMode() const {
    return m_core.repeat();
}

std::string PlaybackCoreTransport::title() const {
    const auto media = m_core.currentMedia();
    if (!media.has_value()) {
        return {};
    }
    return mediaDisplayName(*media);
}

std::optional<playback::MediaRef> PlaybackCoreTransport::currentMedia() const {
    return m_core.currentMedia();
}

void PlaybackCoreTransport::onStateChanged(playback::PlaybackState state) {
    emit stateChanged(state);
}

void PlaybackCoreTransport::onTrackChanged(const playback::MediaRef& media) {
    emit trackChanged(QString::fromStdString(mediaDisplayName(media)));
}

void PlaybackCoreTransport::onPositionChanged(std::int64_t positionMs, std::int64_t lengthMs) {
    emit positionChanged(positionMs, lengthMs);
}

void PlaybackCoreTransport::onVolumeChanged(int volume100) {
    emit volumeChanged(volume100);
}

void PlaybackCoreTransport::onError(std::string_view what) {
    emit errorRaised(QString::fromUtf8(what.data(), static_cast<int>(what.size())));
}

} // namespace koalaamp::ui
