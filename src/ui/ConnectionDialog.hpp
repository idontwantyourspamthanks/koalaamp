// SPDX-License-Identifier: MIT
// ConnectionDialog — the skinned server-connection manager (SV-1).
//
// A gen window like the visualizer's (same chrome, same shape rules), hosting the
// smallest useful form: the saved server list, add-by-URL + credentials, Discover,
// Remove. Signing in calls `JellyfinClient::authenticate`, records identity in the
// `ServerBook` (never the token) and, when a `SecretStore` is supplied, the access
// token in that store so the next launch is already signed in.
//
// v1 scope, deliberately: integer scale 1, synchronous discovery (the UDP window is
// two seconds; a worker lands with the rest of the async surface), and no Plex. The
// widgets are plain Qt controls themed by position, not by skin bitmaps — LI-3 owns
// the `genex`-skinned controls.

#pragma once

#include "servers/ServerBook.hpp"
#include "servers/SecretStore.hpp"
#include "servers/IServerSource.hpp"
#include "servers/jellyfin/DeviceIdentity.hpp"
#include "servers/jellyfin/HttpTransport.hpp"
#include "skins/SkinSet.hpp"
#include "ui/SkinnedWindow.hpp"

#include <QPoint>
#include <QSize>
#include <QString>
#include <QTimer>

#include <optional>

namespace koalaamp::servers::jellyfin {
struct AuthSession;
}

class QLabel;
class QLineEdit;
class QListWidget;
class QMouseEvent;
class QPushButton;

namespace koalaamp::ui {

class ConnectionDialog final : public SkinnedWindow {
    Q_OBJECT

public:
    /// `machineIdPath` overrides the machine-id store (tests use a temp file; the
    /// app passes nothing and gets the default state-dir path). `secrets` is the
    /// OS keychain (or a test double); nullptr keeps the token in this object only.
    ConnectionDialog(servers::jellyfin::IHttpTransport& transport, servers::ServerBook& book,
                     std::string machineIdPath = {}, QWidget* parent = nullptr,
                     servers::SecretStore* secrets = nullptr);

    /// The skin the chrome and title font come from; nullptr paints the fallback.
    void setSkin(const skins::SkinSet* skin);

    /// The signed-in account after a successful login or a keychain restore.
    /// The token is never written to the book.
    const std::optional<servers::ServerAuth>& session() const { return m_session; }

    QSize contentSize() const { return m_contentSize; }

signals:
    /// A login succeeded (or the session was cleared by removing its server).
    void sessionChanged();
    /// The user asked to browse the signed-in server (the Browse button).
    void browseRequested();

private slots:
    /// One QuickConnect poll; also invoked by the test after clicking the button.
    void pollQuickConnect();

private:
    void paintEvent(QPaintEvent* event) override;
    bool isTitleBarAt(const QPoint& point) const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool closeHit(const QPoint& point) const;

    void login();
    void discover();
    void removeSelected();
    void browseServer();
    /// Starts (or cancels) the QuickConnect handshake.
    void quickConnect();
    /// Completion marshalled from the initiate worker (GUI thread).
    void onQuickConnectInitiated(bool ok, const QString& secret, const QString& code,
                                 const QString& reason, const QString& baseUrl,
                                 const QString& deviceId);
    void finishLogin(const std::string& baseUrl,
                     const servers::jellyfin::AuthSession& session, const std::string& deviceId);
    void stopQuickConnect(const QString& message);
    /// Fills URL/user from the selected saved server and focuses the password.
    void fillFromSelection();
    void refreshList();
    void setStatus(const QString& text);

    servers::jellyfin::IHttpTransport& m_transport;
    servers::ServerBook& m_book;
    servers::SecretStore* m_secrets = nullptr;
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
    QPushButton* m_browse = nullptr;
    QPushButton* m_quickConnect = nullptr;
    QLabel* m_status = nullptr;

    QTimer m_quickConnectTimer;
    QString m_quickConnectBaseUrl;
    QString m_quickConnectDeviceId;
    QString m_quickConnectSecret;
    int m_quickConnectPolls = 0;
    bool m_quickConnectPending = false;

    QSize m_contentSize{420, 260};
    bool m_closePressed = false;
};

} // namespace koalaamp::ui
