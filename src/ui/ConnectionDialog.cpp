// SPDX-License-Identifier: MIT
// ConnectionDialog — see ConnectionDialog.hpp.

#include "ui/ConnectionDialog.hpp"

#include "servers/jellyfin/AuthHeaders.hpp"
#include "servers/jellyfin/Discovery.hpp"
#include "servers/jellyfin/JellyfinClient.hpp"
#include "ui/GenRenderer.hpp"

#include <QApplication>
#include <QColor>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QUrl>

#include <chrono>
#include <cstdio>
#include <thread>

#include <algorithm>
#include <variant>

namespace koalaamp::ui {
namespace {

using namespace koalaamp::servers;

/// Identity-only palette helpers, mirroring the visualizer's fallback rule: a skin
/// without `gen.bmp` borrows its playlist colours, and a skin with neither keeps
/// neutral greys.
const skins::Skin& noSheets() {
    static const skins::Skin empty;
    return empty;
}

const skins::PleditColors* pleditIfShipped(const skins::SkinSet* set) {
    if (set == nullptr) {
        return nullptr;
    }
    const auto& missing = set->missingOptional;
    if (std::find(missing.begin(), missing.end(), "pledit.txt") != missing.end()) {
        return nullptr;
    }
    return &set->pleditColors;
}

QString displayHost(const std::string& baseUrl) {
    const QUrl url(QString::fromStdString(baseUrl));
    return url.host().isEmpty() ? QString::fromStdString(baseUrl) : url.host();
}

} // namespace

ConnectionDialog::ConnectionDialog(servers::jellyfin::IHttpTransport& transport,
                                   servers::ServerBook& book, std::string machineIdPath,
                                   QWidget* parent, servers::SecretStore* secrets)
    : SkinnedWindow(parent),
      m_transport(transport),
      m_book(book),
      m_secrets(secrets),
      m_machineId(servers::jellyfin::MachineIdStore(std::move(machineIdPath))) {
    m_contentSize = genSnapSize(QSize(420, 300));
    setFixedSize(m_contentSize);

    const GenLayout layout = genLayout(m_contentSize);
    const QRect client = layout.client;
    const int x = client.x() + 7;
    const int width = client.width() - 14;

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("serverList"));
    m_list->setGeometry(x, client.y() + 6, width, 92);

    m_url = new QLineEdit(this);
    m_url->setObjectName(QStringLiteral("urlField"));
    m_url->setPlaceholderText(QStringLiteral("http://server:8096"));
    m_url->setGeometry(x, client.y() + 104, width, 24);

    m_user = new QLineEdit(this);
    m_user->setObjectName(QStringLiteral("userField"));
    m_user->setPlaceholderText(QStringLiteral("User name"));
    m_user->setGeometry(x, client.y() + 132, width / 2 - 4, 24);

    m_password = new QLineEdit(this);
    m_password->setObjectName(QStringLiteral("passwordField"));
    m_password->setPlaceholderText(QStringLiteral("Password"));
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setGeometry(x + width / 2 + 4, client.y() + 132, width / 2 - 4, 24);

    // Four buttons on one row: the Browse entry lives here as well as in the
    // main menu — signing in and then hunting a right-click menu was a UX dead end.
    const int buttonWidth = width / 4 - 6;
    m_discover = new QPushButton(QStringLiteral("Discover"), this);
    m_discover->setObjectName(QStringLiteral("discoverButton"));
    m_discover->setGeometry(x, client.y() + 162, buttonWidth, 26);

    m_login = new QPushButton(QStringLiteral("Login"), this);
    m_login->setObjectName(QStringLiteral("loginButton"));
    m_login->setGeometry(x + width / 4, client.y() + 162, buttonWidth, 26);

    m_remove = new QPushButton(QStringLiteral("Remove"), this);
    m_remove->setObjectName(QStringLiteral("removeButton"));
    m_remove->setGeometry(x + 2 * (width / 4), client.y() + 162, buttonWidth, 26);

    m_browse = new QPushButton(QStringLiteral("Browse"), this);
    m_browse->setObjectName(QStringLiteral("browseButton"));
    m_browse->setGeometry(x + 3 * (width / 4), client.y() + 162, buttonWidth, 26);
    m_browse->setEnabled(false); // until a login succeeds

    m_quickConnect = new QPushButton(QStringLiteral("QuickConnect"), this);
    m_quickConnect->setObjectName(QStringLiteral("quickConnectButton"));
    m_quickConnect->setGeometry(x, client.y() + 192, width, 26);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("statusLabel"));
    m_status->setGeometry(x, client.y() + 222, width, 20);
    // The gen client area is dark; the default label colour is black, which made
    // every status message (QuickConnect codes included) invisible.
    m_status->setStyleSheet(QStringLiteral("color: #e8e8e8;"));
    m_status->setText(QStringLiteral("No server selected"));

    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int) { fillFromSelection(); });
    connect(m_password, &QLineEdit::returnPressed, this, &ConnectionDialog::login);
    connect(m_login, &QPushButton::clicked, this, &ConnectionDialog::login);
    connect(m_discover, &QPushButton::clicked, this, &ConnectionDialog::discover);
    connect(m_remove, &QPushButton::clicked, this, &ConnectionDialog::removeSelected);
    connect(m_browse, &QPushButton::clicked, this, &ConnectionDialog::browseServer);
    connect(m_quickConnect, &QPushButton::clicked, this, &ConnectionDialog::quickConnect);
    m_quickConnectTimer.setInterval(2000);
    connect(&m_quickConnectTimer, &QTimer::timeout, this, &ConnectionDialog::pollQuickConnect);

    refreshList();
}

void ConnectionDialog::setSkin(const skins::SkinSet* skin) {
    m_skin = skin;
    setWindowShape(skin != nullptr ? skin->region : skins::Region{}, false, m_contentSize, 1);
    update();
}

void ConnectionDialog::paintEvent(QPaintEvent*) {
    const skins::Skin& sheets = m_skin != nullptr ? m_skin->sheets : noSheets();
    const QImage chrome = renderGenWindow(sheets, pleditIfShipped(m_skin), m_contentSize,
                                          "CONNECTION MANAGER", isActiveWindow(), 1,
                                          m_closePressed);
    QPainter painter(this);
    painter.drawImage(0, 0, chrome);
}

bool ConnectionDialog::closeHit(const QPoint& point) const {
    return genLayout(m_contentSize).closeButton.contains(point);
}

bool ConnectionDialog::isTitleBarAt(const QPoint& point) const {
    // The close box sits inside the title bar; a press there must not start a drag.
    return genLayout(m_contentSize).title.contains(point) && !closeHit(point);
}

void ConnectionDialog::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && closeHit(event->pos())) {
        m_closePressed = true;
        update();
        event->accept();
        return;
    }
    SkinnedWindow::mousePressEvent(event);
}

void ConnectionDialog::mouseReleaseEvent(QMouseEvent* event) {
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

void ConnectionDialog::setStatus(const QString& text) {
    m_status->setText(text);
}

void ConnectionDialog::refreshList() {
    m_list->clear();
    const auto& entries = m_book.entries();
    for (const auto& entry : entries) {
        const QString label = QStringLiteral("%1 — %2")
                                  .arg(QString::fromStdString(entry.name))
                                  .arg(QString::fromStdString(entry.baseUrl));
        auto* item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, QString::fromStdString(entry.serverId));
    }
    if (const auto* active = m_book.active(); active != nullptr) {
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(Qt::UserRole).toString() ==
                QString::fromStdString(active->serverId)) {
                m_list->setCurrentRow(i);
                fillFromSelection(); // prefill after an app restart
                break;
            }
        }
    }
}

void ConnectionDialog::login() {
    using namespace koalaamp::servers::jellyfin;

    std::string baseUrl = m_url->text().trimmed().toStdString();
    if (!baseUrl.empty() && baseUrl.find("://") == std::string::npos) {
        // "host:8096" is what people type and what some discovery replies carry;
        // curl needs a scheme, and without one the failure surfaces as unreachable.
        baseUrl = "http://" + baseUrl;
    }
    const std::string user = m_user->text().trimmed().toStdString();
    const std::string password = m_password->text().toStdString();
    if (baseUrl.empty() || user.empty()) {
        setStatus(QStringLiteral("A server URL and user name are required"));
        return;
    }

    const std::string deviceId = deriveDeviceId(m_machineId.machineId(), user);
    DeviceProfile profile;
    JellyfinClient client(m_transport, profile);

    const AuthOutcome outcome = client.authenticate(baseUrl, user, password, deviceId);
    if (const auto* success = std::get_if<AuthSuccess>(&outcome)) {
        finishLogin(baseUrl, success->session, deviceId);
        return;
    }
    if (std::holds_alternative<AuthInvalidCredentials>(outcome)) {
        // Show the server's own reason: a rejected header and a wrong password are
        // both 401s, and calling every one of them "wrong password" hid the difference.
        const auto& rejected = std::get<AuthInvalidCredentials>(outcome);
        setStatus(QString::fromStdString(rejected.message));
        std::fprintf(stderr, "koalaamp: login rejected at %s: %s\n", baseUrl.c_str(),
                     rejected.message.c_str());
    } else if (std::holds_alternative<AuthUnreachable>(outcome)) {
        setStatus(QStringLiteral("Server unreachable"));
    } else {
        const auto& failed = std::get<AuthFailed>(outcome);
        setStatus(QString::fromStdString(failed.message));
    }
}

void ConnectionDialog::fillFromSelection() {
    const QListWidgetItem* item = m_list->currentItem();
    if (item == nullptr) {
        return;
    }
    const std::string serverId = item->data(Qt::UserRole).toString().toStdString();
    for (const auto& entry : m_book.entries()) {
        if (entry.serverId != serverId) {
            continue;
        }
        m_url->setText(QString::fromStdString(entry.baseUrl));
        m_user->setText(QString::fromStdString(entry.userName));
        m_password->clear();
        if (m_secrets != nullptr) {
            if (const auto token =
                    m_secrets->load(servers::sessionSecretKey(entry.serverId, entry.baseUrl));
                token && !token->empty()) {
                const bool changed = !m_session || m_session->accessToken != *token ||
                                     m_session->serverId != entry.serverId;
                m_session = servers::ServerAuth{entry.baseUrl, entry.userId, *token, entry.deviceId,
                                                entry.serverId};
                m_browse->setEnabled(true);
                setStatus(QStringLiteral("Signed in as %1")
                              .arg(QString::fromStdString(entry.userName)));
                if (changed) {
                    emit sessionChanged();
                }
                return;
            }
            if (m_session) {
                m_session.reset();
                m_browse->setEnabled(false);
                emit sessionChanged();
            }
        }
        m_password->setFocus();
        setStatus(QStringLiteral("Enter the password for %1")
                      .arg(QString::fromStdString(entry.userName)));
        return;
    }
}

void ConnectionDialog::finishLogin(const std::string& baseUrl,
                                   const servers::jellyfin::AuthSession& session,
                                   const std::string& deviceId) {
    const std::string serverId = session.serverId.empty() ? baseUrl : session.serverId;
    m_session = servers::ServerAuth{baseUrl, session.userId, session.accessToken, deviceId, serverId};
    bool keyringSaved = true;
    if (m_secrets != nullptr) {
        keyringSaved = servers::rememberSession(*m_secrets, *m_session);
    }

    servers::ServerEntry entry;
    entry.serverId = serverId;
    entry.name = displayHost(baseUrl).toStdString();
    entry.baseUrl = baseUrl;
    entry.userId = session.userId;
    entry.userName = session.userName;
    entry.deviceId = deviceId;
    m_book.upsert(std::move(entry));
    const bool saved = m_book.save();

    refreshList();
    m_password->clear();
    m_browse->setEnabled(true);
    m_browse->setFocus(); // Enter opens the library right after signing in
    if (m_secrets != nullptr && !keyringSaved) {
        setStatus(QStringLiteral("Signed in as %1 — the keyring did not store this sign-in")
                      .arg(QString::fromStdString(session.userName)));
    } else {
        setStatus(saved ? QStringLiteral("Signed in as %1 — saved")
                              .arg(QString::fromStdString(session.userName))
                        : QStringLiteral("Signed in, but the server list could not be saved"));
    }
    emit sessionChanged();
}

void ConnectionDialog::stopQuickConnect(const QString& message) {
    m_quickConnectTimer.stop();
    m_quickConnectSecret.clear();
    m_quickConnect->setText(QStringLiteral("QuickConnect"));
    if (!message.isEmpty()) {
        setStatus(message);
    }
}

void ConnectionDialog::quickConnect() {
    using namespace koalaamp::servers::jellyfin;

    std::fprintf(stderr, "koalaamp: QuickConnect clicked\n");
    if (m_quickConnectTimer.isActive()) {
        stopQuickConnect(QStringLiteral("QuickConnect cancelled"));
        return;
    }

    std::string baseUrl = m_url->text().trimmed().toStdString();
    if (!baseUrl.empty() && baseUrl.find("://") == std::string::npos) {
        baseUrl = "http://" + baseUrl;
    }
    if (baseUrl.empty()) {
        setStatus(QStringLiteral("A server URL is required"));
        QMessageBox::warning(this, QStringLiteral("QuickConnect"),
                             QStringLiteral("Enter the server URL first."));
        m_url->setFocus();
        return;
    }

    if (m_quickConnectPending) {
        return; // a request is already in flight; ignore the extra click
    }

    // Visible acknowledgement before the request: a click that changes nothing
    // reads as a broken button.
    m_quickConnect->setText(QStringLiteral("QuickConnect…"));
    m_quickConnect->repaint();
    QApplication::processEvents();

    // QuickConnect is pre-auth: there is no user yet, so the device id is
    // derived from the machine alone and stays stable for the token's lifetime.
    const std::string deviceId = deriveDeviceId(m_machineId.machineId(), "");
    const std::string requestBaseUrl = baseUrl;

    // The call is bounded (5 s connect / 8 s total), but it still must not block
    // the GUI thread: run it and marshal the result back. `qApp` is the queued
    // call's context so a closed dialog never receives the completion.
    m_quickConnectPending = true;
    const QPointer<ConnectionDialog> self(this);
    IHttpTransport* transport = &m_transport;
    std::thread([self, transport, requestBaseUrl, deviceId] {
        using namespace koalaamp::servers::jellyfin;
        const auto started = std::chrono::steady_clock::now();
        JellyfinClient client(*transport, DeviceProfile{});
        const QuickConnectStartOutcome outcome =
            client.initiateQuickConnect(requestBaseUrl, deviceId);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        std::fprintf(stderr, "koalaamp: QuickConnect initiate returned in %lld ms\n",
                     static_cast<long long>(elapsed));

        bool ok = false;
        QString secret;
        QString code;
        QString reason;
        if (const auto* start = std::get_if<QuickConnectStart>(&outcome)) {
            ok = true;
            secret = QString::fromStdString(start->secret);
            code = QString::fromStdString(start->code);
        } else if (std::holds_alternative<ItemsUnauthorized>(outcome)) {
            reason = QStringLiteral(
                "QuickConnect is disabled on this server, or it rejected the request");
        } else if (std::holds_alternative<ItemsUnreachable>(outcome)) {
            reason = QStringLiteral("Server unreachable");
        } else {
            reason = QString::fromStdString(std::get<ItemsFailed>(outcome).message);
        }
        QMetaObject::invokeMethod(
            qApp,
            [self, ok, secret, code, reason, requestBaseUrl = QString::fromStdString(requestBaseUrl),
             deviceId = QString::fromStdString(deviceId)] {
                if (self) {
                    self->onQuickConnectInitiated(ok, secret, code, reason, requestBaseUrl, deviceId);
                }
            },
            Qt::QueuedConnection);
    }).detach();
}

void ConnectionDialog::onQuickConnectInitiated(bool ok, const QString& secret,
                                               const QString& code, const QString& reason,
                                               const QString& baseUrl, const QString& deviceId) {
    m_quickConnectPending = false;
    if (!ok) {
        std::fprintf(stderr, "koalaamp: QuickConnect failed at %s: %s\n",
                     baseUrl.toStdString().c_str(), reason.toStdString().c_str());
        m_quickConnect->setText(QStringLiteral("QuickConnect"));
        setStatus(reason);
        QMessageBox::warning(this, QStringLiteral("QuickConnect"), reason);
        return;
    }

    m_quickConnectBaseUrl = baseUrl;
    m_quickConnectDeviceId = deviceId;
    m_quickConnectSecret = secret;
    m_quickConnectPolls = 0;
    // The code is the whole point of the flow; put it on the button as well as
    // the status so it cannot be missed at the bottom of the window.
    m_quickConnect->setText(QStringLiteral("Code %1 — cancel").arg(code));
    m_quickConnectTimer.start();
    setStatus(QStringLiteral("Approve code %1 in Jellyfin (Quick Connect)").arg(code));
    std::fprintf(stderr, "koalaamp: QuickConnect code %s at %s\n", code.toStdString().c_str(),
                 baseUrl.toStdString().c_str());
}

void ConnectionDialog::pollQuickConnect() {
    using namespace koalaamp::servers::jellyfin;
    if (m_quickConnectSecret.isEmpty()) {
        return;
    }
    const std::string baseUrl = m_quickConnectBaseUrl.toStdString();
    const std::string deviceId = m_quickConnectDeviceId.toStdString();
    const std::string secret = m_quickConnectSecret.toStdString();

    JellyfinClient client(m_transport, DeviceProfile{});
    const QuickConnectStateOutcome outcome = client.quickConnectState(baseUrl, deviceId, secret);
    if (const auto* state = std::get_if<QuickConnectState>(&outcome)) {
        if (!state->authenticated) {
            if (++m_quickConnectPolls >= 60) { // two minutes
                stopQuickConnect(QStringLiteral("QuickConnect timed out"));
            }
            return;
        }
        stopQuickConnect(QString());
        const AuthOutcome auth = client.authenticateWithQuickConnect(baseUrl, secret, deviceId);
        if (const auto* success = std::get_if<AuthSuccess>(&auth)) {
            finishLogin(baseUrl, success->session, deviceId);
            return;
        }
        if (std::holds_alternative<AuthInvalidCredentials>(auth)) {
            setStatus(QString::fromStdString(
                std::get<AuthInvalidCredentials>(auth).message));
        } else if (std::holds_alternative<AuthUnreachable>(auth)) {
            setStatus(QStringLiteral("Server unreachable"));
        } else {
            setStatus(QString::fromStdString(std::get<AuthFailed>(auth).message));
        }
        return;
    }
    if (std::holds_alternative<ItemsUnreachable>(outcome)) {
        stopQuickConnect(QStringLiteral("Server unreachable"));
    } else if (std::holds_alternative<ItemsUnauthorized>(outcome)) {
        stopQuickConnect(QStringLiteral("The server rejected the request"));
    } else {
        stopQuickConnect(QString::fromStdString(std::get<ItemsFailed>(outcome).message));
    }
}

void ConnectionDialog::browseServer() {
    if (!m_session) {
        setStatus(QStringLiteral("Sign in first"));
        return;
    }
    emit browseRequested();
}

void ConnectionDialog::discover() {
    const auto found = servers::jellyfin::discoverServers();
    if (found.empty()) {
        setStatus(QStringLiteral("No Jellyfin servers answered on the LAN"));
        return;
    }
    if (found.size() == 1) {
        const auto& server = found.front();
        m_url->setText(QString::fromStdString(server.address));
        const QString name = QString::fromStdString(server.name);
        setStatus(name.isEmpty() ? QStringLiteral("Found one server")
                                 : QStringLiteral("Found %1").arg(name));
        return;
    }

    // Several answered. Picking silently by order is what made Discover look like it
    // had done nothing; ask instead.
    QStringList labels;
    labels.reserve(static_cast<qsizetype>(found.size()));
    for (const auto& server : found) {
        const QString name = QString::fromStdString(server.name);
        labels << QStringLiteral("%1 — %2")
                      .arg(name.isEmpty() ? QStringLiteral("Jellyfin") : name,
                           QString::fromStdString(server.address));
    }
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(this, QStringLiteral("Discover servers"),
                                                 QStringLiteral("Pick a server to use:"), labels,
                                                 0, false, &accepted);
    if (!accepted) {
        setStatus(QStringLiteral("Discovery cancelled"));
        return;
    }
    const qsizetype index = labels.indexOf(chosen);
    if (index < 0 || static_cast<std::size_t>(index) >= found.size()) {
        return;
    }
    m_url->setText(QString::fromStdString(found[static_cast<std::size_t>(index)].address));
    setStatus(QStringLiteral("Selected %1").arg(chosen));
}

void ConnectionDialog::removeSelected() {
    const auto* item = m_list->currentItem();
    if (item == nullptr) {
        setStatus(QStringLiteral("Select a server to remove"));
        return;
    }
    const std::string serverId = item->data(Qt::UserRole).toString().toStdString();
    std::string baseUrl;
    for (const auto& entry : m_book.entries()) {
        if (entry.serverId == serverId) {
            baseUrl = entry.baseUrl;
            break;
        }
    }
    if (!m_book.remove(serverId)) {
        setStatus(QStringLiteral("That server is no longer in the list"));
        return;
    }
    if (m_secrets != nullptr) {
        servers::forgetSession(*m_secrets, serverId, baseUrl);
    }
    m_book.save();
    m_session.reset();
    m_browse->setEnabled(false);
    refreshList();
    setStatus(QStringLiteral("Removed"));
    emit sessionChanged();
}

} // namespace koalaamp::ui
