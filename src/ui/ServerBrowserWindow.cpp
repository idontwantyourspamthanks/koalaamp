// SPDX-License-Identifier: MIT
// ServerBrowserWindow — see ServerBrowserWindow.hpp.

#include "ui/ServerBrowserWindow.hpp"

#include "ui/GenRenderer.hpp"

#include <QAction>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <utility>
#include <variant>

namespace koalaamp::ui {
namespace {

using namespace koalaamp::servers;
using namespace koalaamp::servers::jellyfin;

template <typename Outcome>
QString describeFailure(const Outcome& outcome) {
    if (std::holds_alternative<ItemsUnauthorized>(outcome)) {
        return QStringLiteral("The server rejected the token");
    }
    if (std::holds_alternative<ItemsUnreachable>(outcome)) {
        return QStringLiteral("Server unreachable");
    }
    return QString::fromStdString(std::get<ItemsFailed>(outcome).message);
}

const skins::Skin& noSheets() {
    static const skins::Skin empty;
    return empty;
}

QString trackLabel(const library::Track& track) {
    if (track.artist.empty()) {
        return QString::fromStdString(track.title);
    }
    return QStringLiteral("%1 — %2")
        .arg(QString::fromStdString(track.title), QString::fromStdString(track.artist));
}

} // namespace

ServerBrowserWindow::ServerBrowserWindow(IHttpTransport& transport, ServerAuth session,
                                         PlayHandler play, QWidget* parent)
    : SkinnedWindow(parent),
      m_transport(transport),
      m_session(std::move(session)),
      m_client(transport, [] {
          DeviceProfile profile;
          profile.deviceName = defaultDeviceName();
          return profile;
      }()),
      m_play(std::move(play)) {
    m_contentSize = genSnapSize(QSize(700, 400));
    setFixedSize(m_contentSize);

    const GenLayout layout = genLayout(m_contentSize);
    const QRect client = layout.client;
    const int margin = 7;
    const int statusHeight = 18;
    const int headerHeight = 14;
    const int gap = 8;
    const int contentHeight = client.height() - margin * 2 - statusHeight - headerHeight - gap;

    const int leftWidth = 150;
    const int middleWidth = 190;
    const int rightWidth =
        client.width() - margin * 2 - leftWidth - middleWidth - gap * 2;
    const int leftX = client.x() + margin;
    const int middleX = leftX + leftWidth + gap;
    const int rightX = middleX + middleWidth + gap;
    const int headerY = client.y() + margin;
    const int listY = headerY + headerHeight;

    auto addHeader = [&](int x, int width, const QString& text) {
        auto* label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("browserHeader"));
        label->setGeometry(x, headerY, width, headerHeight);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    };
    addHeader(leftX, leftWidth, QStringLiteral("LIBRARIES"));
    addHeader(middleX, middleWidth, QStringLiteral("ALBUMS"));
    addHeader(rightX, rightWidth, QStringLiteral("TRACKS"));

    // Libraries and playlists share the left column: the top half is views,
    // the bottom half is user playlists.
    const int half = (contentHeight - gap) / 2;
    m_libraries = new QListWidget(this);
    m_libraries->setObjectName(QStringLiteral("libraryList"));
    m_libraries->setGeometry(leftX, listY, leftWidth, half);

    m_playlists = new QListWidget(this);
    m_playlists->setObjectName(QStringLiteral("playlistList"));
    m_playlists->setGeometry(leftX, listY + half + gap, leftWidth, contentHeight - half - gap);

    m_albums = new QListWidget(this);
    m_albums->setObjectName(QStringLiteral("albumList"));
    m_albums->setGeometry(middleX, listY, middleWidth, contentHeight);

    m_tracks = new QListWidget(this);
    m_tracks->setObjectName(QStringLiteral("trackList"));
    m_tracks->setGeometry(rightX, listY, rightWidth, contentHeight);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("browserStatus"));
    m_status->setGeometry(leftX, listY + contentHeight + gap,
                          client.width() - margin * 2, statusHeight);
    m_status->setText(QStringLiteral("Loading…"));

    connect(m_libraries, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item != nullptr) {
            loadAlbums(item->data(Qt::UserRole).toString());
        }
    });
    connect(m_playlists, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item != nullptr) {
            loadPlaylist(item->data(Qt::UserRole).toString());
        }
    });
    connect(m_albums, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item != nullptr) {
            loadAlbum(item->data(Qt::UserRole).toString());
        }
    });
    connect(m_tracks, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) { playTrackRow(m_tracks->row(item)); });

    m_tracks->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tracks, &QListWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        const QListWidgetItem* item = m_tracks->itemAt(point);
        if (item == nullptr) {
            return;
        }
        const int row = m_tracks->row(item);
        QMenu menu(m_tracks);
        QAction* now = menu.addAction(QStringLiteral("Play now"));
        QAction* next = menu.addAction(QStringLiteral("Play next"));
        QAction* chosen = menu.exec(m_tracks->mapToGlobal(point));
        if (chosen == now) {
            playTrackRow(row);
        } else if (chosen == next && row >= 0 &&
                   static_cast<std::size_t>(row) < m_loadedTracks.size()) {
            m_play({m_loadedTracks[static_cast<std::size_t>(row)]}, 0, true);
        }
    });

    refreshLibraries();
    loadPlaylists();
}

void ServerBrowserWindow::setSkin(const skins::SkinSet* skin) {
    m_skin = skin;
    setWindowShape(skin != nullptr ? skin->region : skins::Region{}, false, m_contentSize, 1);
    update();
}

void ServerBrowserWindow::paintEvent(QPaintEvent*) {
    const skins::Skin& sheets = m_skin != nullptr ? m_skin->sheets : noSheets();
    const QImage chrome =
        renderGenWindow(sheets, nullptr, m_contentSize, "SERVER BROWSER", isActiveWindow(), 1,
                        m_closePressed);
    QPainter painter(this);
    painter.drawImage(0, 0, chrome);
}

bool ServerBrowserWindow::closeHit(const QPoint& point) const {
    return genLayout(m_contentSize).closeButton.contains(point);
}

bool ServerBrowserWindow::isTitleBarAt(const QPoint& point) const {
    return genLayout(m_contentSize).title.contains(point) && !closeHit(point);
}

void ServerBrowserWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && closeHit(event->pos())) {
        m_closePressed = true;
        update();
        event->accept();
        return;
    }
    SkinnedWindow::mousePressEvent(event);
}

void ServerBrowserWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (m_closePressed) {
        const bool inside = closeHit(event->pos());
        m_closePressed = false;
        update();
        if (inside) {
            close();
        }
        event->accept();
        return;
    }
    SkinnedWindow::mouseReleaseEvent(event);
}

void ServerBrowserWindow::setStatus(const QString& text) {
    m_status->setText(text);
}

void ServerBrowserWindow::refreshLibraries() {
    const LibrariesOutcome outcome =
        m_client.musicLibraries(m_session.baseUrl, m_session.accessToken, m_session.deviceId,
                                m_session.userId);
    m_libraries->clear();
    if (const auto* success = std::get_if<LibrariesSuccess>(&outcome)) {
        int rows = 0;
        for (const MusicLibrary& library : success->libraries) {
            if (library.collectionType != "music") {
                continue;
            }
            auto* item = new QListWidgetItem(QString::fromStdString(library.name), m_libraries);
            item->setData(Qt::UserRole, QString::fromStdString(library.id));
            ++rows;
        }
        if (rows == 0) {
            setStatus(QStringLiteral("No music libraries on this server"));
        } else {
            m_libraries->setCurrentRow(0);
            loadAlbums(m_libraries->item(0)->data(Qt::UserRole).toString());
        }
    } else {
        setStatus(describeFailure(outcome));
        if (std::holds_alternative<ItemsUnauthorized>(outcome)) {
            emit sessionExpired();
        }
    }
}

void ServerBrowserWindow::loadAlbums(const QString& libraryId) {
    const ItemsOutcome outcome =
        m_client.musicAlbumsWindow(m_session.baseUrl, m_session.accessToken, m_session.deviceId,
                                   libraryId.toStdString(), 0, 500);
    m_albums->clear();
    m_tracks->clear();
    m_loadedTracks.clear();
    if (const auto* success = std::get_if<ItemsSuccess>(&outcome)) {
        for (const MediaItem& album : success->window.items) {
            auto* item = new QListWidgetItem(QString::fromStdString(album.name), m_albums);
            item->setData(Qt::UserRole, QString::fromStdString(album.id));
            if (album.productionYear > 0) {
                item->setToolTip(QString::number(album.productionYear));
            }
        }
        setStatus(QStringLiteral("%1 albums").arg(m_albums->count()));
    } else {
        setStatus(describeFailure(outcome));
        if (std::holds_alternative<ItemsUnauthorized>(outcome)) {
            emit sessionExpired();
        }
    }
}

void ServerBrowserWindow::loadPlaylists() {
    const NamedItemsOutcome outcome =
        m_client.playlists(m_session.baseUrl, m_session.accessToken, m_session.deviceId);
    m_playlists->clear();
    if (const auto* success = std::get_if<NamedItemsSuccess>(&outcome)) {
        for (const NamedItem& playlist : success->items) {
            auto* item = new QListWidgetItem(QString::fromStdString(playlist.name), m_playlists);
            item->setData(Qt::UserRole, QString::fromStdString(playlist.id));
        }
    }
}

void ServerBrowserWindow::loadAlbum(const QString& albumId) {
    const TracksOutcome outcome =
        m_client.albumTracks(m_session.baseUrl, m_session.accessToken, m_session.deviceId,
                             albumId.toStdString());
    m_tracks->clear();
    m_loadedTracks.clear();
    if (const auto* success = std::get_if<TracksSuccess>(&outcome)) {
        m_loadedTracks = success->tracks;
        for (const library::Track& track : m_loadedTracks) {
            auto* item = new QListWidgetItem(trackLabel(track), m_tracks);
            item->setToolTip(QString::fromStdString(track.album));
        }
        setStatus(QStringLiteral("%1 tracks").arg(m_tracks->count()));
    } else {
        setStatus(describeFailure(outcome));
        if (std::holds_alternative<ItemsUnauthorized>(outcome)) {
            emit sessionExpired();
        }
    }
}

void ServerBrowserWindow::loadPlaylist(const QString& playlistId) {
    const TracksOutcome outcome =
        m_client.playlistItems(m_session.baseUrl, m_session.accessToken, m_session.deviceId,
                               playlistId.toStdString(), m_session.userId);
    m_albums->clear();
    m_tracks->clear();
    m_loadedTracks.clear();
    if (const auto* success = std::get_if<TracksSuccess>(&outcome)) {
        m_loadedTracks = success->tracks;
        for (const library::Track& track : m_loadedTracks) {
            auto* item = new QListWidgetItem(trackLabel(track), m_tracks);
            item->setToolTip(QString::fromStdString(track.album));
        }
        setStatus(QStringLiteral("Playlist: %1 tracks").arg(m_tracks->count()));
    } else {
        setStatus(describeFailure(outcome));
        if (std::holds_alternative<ItemsUnauthorized>(outcome)) {
            emit sessionExpired();
        }
    }
}

void ServerBrowserWindow::playTrackRow(int row) {
    if (row < 0 || static_cast<std::size_t>(row) >= m_loadedTracks.size()) {
        return;
    }
    if (m_play) {
        m_play(m_loadedTracks, static_cast<std::size_t>(row), false);
    }
}

} // namespace koalaamp::ui
