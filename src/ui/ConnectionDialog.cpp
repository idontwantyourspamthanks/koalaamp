// SPDX-License-Identifier: MIT
// ConnectionDialog — see ConnectionDialog.hpp.

#include "ui/ConnectionDialog.hpp"

#include "servers/jellyfin/AuthHeaders.hpp"
#include "servers/jellyfin/Discovery.hpp"
#include "servers/jellyfin/JellyfinClient.hpp"
#include "ui/GenRenderer.hpp"

#include <QColor>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QUrl>

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
                                   QWidget* parent)
    : SkinnedWindow(parent),
      m_transport(transport),
      m_book(book),
      m_machineId(servers::jellyfin::MachineIdStore(std::move(machineIdPath))) {
    m_contentSize = genSnapSize(QSize(420, 260));
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

    m_discover = new QPushButton(QStringLiteral("Discover"), this);
    m_discover->setObjectName(QStringLiteral("discoverButton"));
    m_discover->setGeometry(x, client.y() + 162, width / 3 - 6, 26);

    m_login = new QPushButton(QStringLiteral("Login"), this);
    m_login->setObjectName(QStringLiteral("loginButton"));
    m_login->setGeometry(x + width / 3, client.y() + 162, width / 3 - 6, 26);

    m_remove = new QPushButton(QStringLiteral("Remove"), this);
    m_remove->setObjectName(QStringLiteral("removeButton"));
    m_remove->setGeometry(x + 2 * (width / 3), client.y() + 162, width / 3 - 6, 26);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("statusLabel"));
    m_status->setGeometry(x, client.y() + 192, width, 20);
    m_status->setText(QStringLiteral("No server selected"));

    connect(m_login, &QPushButton::clicked, this, &ConnectionDialog::login);
    connect(m_discover, &QPushButton::clicked, this, &ConnectionDialog::discover);
    connect(m_remove, &QPushButton::clicked, this, &ConnectionDialog::removeSelected);

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
                                          "CONNECTION MANAGER", isActiveWindow(), 1);
    QPainter painter(this);
    painter.drawImage(0, 0, chrome);
}

bool ConnectionDialog::isTitleBarAt(const QPoint& point) const {
    return genLayout(m_contentSize).title.contains(point);
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
                break;
            }
        }
    }
}

void ConnectionDialog::login() {
    using namespace koalaamp::servers::jellyfin;

    const std::string baseUrl = m_url->text().trimmed().toStdString();
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
        m_session = servers::ServerAuth{baseUrl, success->session.userId,
                                        success->session.accessToken, deviceId};

        servers::ServerEntry entry;
        entry.serverId = success->session.serverId.empty() ? baseUrl : success->session.serverId;
        entry.name = displayHost(baseUrl).toStdString();
        entry.baseUrl = baseUrl;
        entry.userId = success->session.userId;
        entry.userName = success->session.userName;
        entry.deviceId = deviceId;
        m_book.upsert(std::move(entry));
        const bool saved = m_book.save();

        refreshList();
        m_password->clear();
        setStatus(saved ? QStringLiteral("Signed in as %1 — saved")
                              .arg(QString::fromStdString(success->session.userName))
                        : QStringLiteral("Signed in, but the server list could not be saved"));
        emit sessionChanged();
        return;
    }
    if (std::holds_alternative<AuthInvalidCredentials>(outcome)) {
        setStatus(QStringLiteral("Wrong user name or password"));
    } else if (std::holds_alternative<AuthUnreachable>(outcome)) {
        setStatus(QStringLiteral("Server unreachable"));
    } else {
        const auto& failed = std::get<AuthFailed>(outcome);
        setStatus(QString::fromStdString(failed.message));
    }
}

void ConnectionDialog::discover() {
    const auto found = servers::jellyfin::discoverServers();
    if (found.empty()) {
        setStatus(QStringLiteral("No Jellyfin servers answered on the LAN"));
        return;
    }
    m_url->setText(QString::fromStdString(found.front().address));
    setStatus(QStringLiteral("%1 server(s) found").arg(static_cast<int>(found.size())));
}

void ConnectionDialog::removeSelected() {
    const auto* item = m_list->currentItem();
    if (item == nullptr) {
        setStatus(QStringLiteral("Select a server to remove"));
        return;
    }
    const std::string serverId = item->data(Qt::UserRole).toString().toStdString();
    if (!m_book.remove(serverId)) {
        setStatus(QStringLiteral("That server is no longer in the list"));
        return;
    }
    m_book.save();
    m_session.reset();
    refreshList();
    setStatus(QStringLiteral("Removed"));
    emit sessionChanged();
}

} // namespace koalaamp::ui
