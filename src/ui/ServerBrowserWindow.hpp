// SPDX-License-Identifier: MIT
// ServerBrowserWindow — browse a signed-in Jellyfin server (SV-5).
//
// Three columns in the same gen chrome as the connection manager:
//   libraries (top) + playlists (bottom) | albums | tracks
// Double-click a track to play from there; "Play next" queues. Library and
// playlist contents are fetched through the SV-1 gateway; playback itself is
// the caller's callback, so this window owns no engine or reporter state.

#pragma once

#include "servers/IServerSource.hpp"
#include "servers/jellyfin/HttpTransport.hpp"
#include "servers/jellyfin/JellyfinClient.hpp"
#include "skins/SkinSet.hpp"
#include "ui/SkinnedWindow.hpp"

#include <QPoint>
#include <QSize>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class QLabel;
class QListWidget;
class QMouseEvent;
class QPaintEvent;

namespace koalaamp::ui {

class ServerBrowserWindow final : public SkinnedWindow {
    Q_OBJECT

public:
    /// `enqueueNext == true` means "Play next" (front of the queue), otherwise
    /// play from `startIndex` and enqueue the rest.
    using PlayHandler = std::function<void(const std::vector<library::Track>& tracks,
                                           std::size_t startIndex, bool enqueueNext)>;

    ServerBrowserWindow(servers::jellyfin::IHttpTransport& transport,
                        servers::ServerAuth session, PlayHandler play, QWidget* parent = nullptr);

    /// The skin the chrome and labels come from; nullptr paints the fallback.
    void setSkin(const skins::SkinSet* skin);

    QSize contentSize() const { return m_contentSize; }

signals:
    /// The server answered 401: the token is stale and the app should re-auth.
    void sessionExpired();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool isTitleBarAt(const QPoint& point) const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void refreshLibraries();
    void loadAlbums(const QString& libraryId);
    void loadPlaylists();
    void loadAlbum(const QString& albumId);
    void loadPlaylist(const QString& playlistId);
    void playTrackRow(int row);
    void setStatus(const QString& text);
    bool closeHit(const QPoint& point) const;

    servers::jellyfin::IHttpTransport& m_transport;
    servers::ServerAuth m_session;
    servers::jellyfin::JellyfinClient m_client;
    PlayHandler m_play;
    const skins::SkinSet* m_skin = nullptr;

    QSize m_contentSize;
    QListWidget* m_libraries = nullptr;
    QListWidget* m_playlists = nullptr;
    QListWidget* m_albums = nullptr;
    QListWidget* m_tracks = nullptr;
    QLabel* m_status = nullptr;
    std::vector<library::Track> m_loadedTracks;
    bool m_closePressed = false;
};

} // namespace koalaamp::ui
