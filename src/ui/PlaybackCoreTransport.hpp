// SPDX-License-Identifier: MIT
//
// PlaybackCoreTransport — the window's adapter onto `playback::PlaybackCore`.
//
// Commands go straight through the core's thread-safe public surface; events
// arrive on the core's dispatch thread (never a libvlc thread — see
// PlaybackCore.hpp) and are re-emitted as Qt signals so delivery into the GUI
// thread is queued. The PCM tap is installed through
// `IAudioEngine::setPcmCallback` into the window's `VisFeed` (the sanctioned
// DSP-insertion seam); nothing in src/playback or src/interop is touched.
#pragma once

#include <QObject>
#include <QString>

#include "playback/PlaybackCore.hpp"
#include "ui/Transport.hpp"
#include "ui/VisFeed.hpp"

namespace koalaamp::ui {

class PlaybackCoreTransport final : public QObject,
                                    public ITransport,
                                    public playback::IPlaybackObserver {
    Q_OBJECT

public:
    explicit PlaybackCoreTransport(playback::PlaybackCore& core, VisFeed& feed,
                                   QObject* parent = nullptr);
    ~PlaybackCoreTransport() override;

    // --- ITransport ---------------------------------------------------------
    void playMedia(const playback::MediaRef& media) override;
    void resume() override;
    void pause() override;
    void togglePause() override;
    void stop() override;
    void next() override;
    void previous() override;
    void seekMs(std::int64_t positionMs) override;
    void setVolumePercent(int volume) override;
    void setShuffle(bool enabled) override;
    void setRepeatMode(playback::RepeatMode mode) override;

    bool hasMedia() const override;
    playback::PlaybackState state() const override;
    std::int64_t positionMs() const override;
    std::int64_t lengthMs() const override;
    int volumePercent() const override;
    bool shuffle() const override;
    playback::RepeatMode repeatMode() const override;
    std::string title() const override;
    /// libvlc knows the media's bitrate, but `IAudioEngine` has no media-info
    /// query (AU-1 owns that seam), so the field renders Winamp's "  0"
    /// placeholder — exactly what a track without tags shows.
    int bitrateKbps() const override { return 0; }

    // --- IPlaybackObserver (dispatch thread: no GUI, no engine call) --------
    void onStateChanged(playback::PlaybackState state) override;
    void onTrackChanged(const playback::MediaRef& media) override;
    void onPositionChanged(std::int64_t positionMs, std::int64_t lengthMs) override;
    void onVolumeChanged(int volume100) override;
    void onError(std::string_view what) override;

signals:
    void stateChanged(playback::PlaybackState state);
    void trackChanged(QString title);
    void positionChanged(qint64 positionMs, qint64 lengthMs);
    void volumeChanged(int volume100);
    void errorRaised(QString what);

private:
    playback::PlaybackCore& m_core;
    VisFeed& m_feed;
};

}  // namespace koalaamp::ui
