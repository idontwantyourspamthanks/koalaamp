// SPDX-License-Identifier: MIT
// ConnectionDialog — the skinned server-connection manager (SV-1).
//
// A gen window like the visualizer's (same chrome, same shape rules), hosting the
// smallest useful form: the saved server list, add-by-URL + credentials, Discover,
// Remove. Signing in calls `JellyfinClient::authenticate`, records identity in the
// `ServerBook` (never the token) and keeps the session token in this object only.
//
// v1 scope, deliberately: integer scale 1, synchronous discovery (the UDP window is
// two seconds; a worker lands with the rest of the async surface), and no Plex. The
// widgets are plain Qt controls themed by position, not by skin bitmaps — LI-3 owns
// the `genex`-skinned controls.

#pragma once

#include "servers/ServerBook.hpp"
#include "servers/IServerSource.hpp"
#include "servers/jellyfin/DeviceIdentity.hpp"
#include "servers/jellyfin/HttpTransport.hpp"
#include "skins/SkinSet.hpp"
#include "ui/SkinnedWindow.hpp"

#include <QPoint>
#include <QSize>

#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace koalaamp::ui {

class ConnectionDialog final : public SkinnedWindow {
    Q_OBJECT

public:
    /// `machineIdPath` overrides the machine-id store (tests use a temp file; the
    /// app passes nothing and gets the default state-dir path).
    ConnectionDialog(servers::jellyfin::IHttpTransport& transport, servers::ServerBook& book,
                     std::string machineIdPath = {}, QWidget* parent = nullptr);

    /// The skin the chrome and title font come from; nullptr paints the fallback.
    void setSkin(const skins::SkinSet* skin);

    /// The signed-in account after a successful login. The token lives here only —
    /// it is never written to the book.
    const std::optional<servers::ServerAuth>& session() const { return m_session; }

    QSize contentSize() const { return m_contentSize; }

signals:
    /// A login succeeded (or the session was cleared by removing its server).
    void sessionChanged();

private:
    void paintEvent(QPaintEvent* event) override;
    bool isTitleBarAt(const QPoint& point) const override;

    void login();
    void discover();
    void removeSelected();
    void refreshList();
    void setStatus(const QString& text);

    servers::jellyfin::IHttpTransport& m_transport;
    servers::ServerBook& m_book;
    servers::jellyfin::MachineIdStore m_machineId;
    const skins::SkinSet* m_skin = nullptr;
    std::optional<servers::ServerAuth> m_session;

    QListWidget* m_list = nullptr;
    QLineEdit* m_url = nullptr;
    QLineEdit* m_user = nullptr;
    QLineEdit* m_password = nullptr;
    QPushButton* m_login = nullptr;
    QPushButton* m_discover = nullptr;
    QPushButton* m_remove = nullptr;
    QLabel* m_status = nullptr;

    QSize m_contentSize{420, 260};
};

} // namespace koalaamp::ui
