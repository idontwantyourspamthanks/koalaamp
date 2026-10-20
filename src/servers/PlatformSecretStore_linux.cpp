// SPDX-License-Identifier: MIT
// servers — Jellyfin token on Linux.
//
// Secret Service (org.freedesktop.secrets) is the cross-desktop store: GNOME
// keyring and, on this Kubuntu machine, the service KWallet exposes. The
// algorithm is "plain" because the per-user session bus is the trust boundary.
// When that service is not running, the same calls go to KWallet's own D-Bus
// API (org.kde.kwalletd6, then kwalletd5). A missing daemon is a quiet miss;
// sign-in still works for the current process.

#include "servers/SecretStore.hpp"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QEventLoop>
#include <QTimer>
#include <QVariantMap>

#include <cstdio>
#include <string>
#include <utility>

namespace koalaamp::servers {
namespace {

constexpr auto kServiceName = "org.freedesktop.secrets";
constexpr auto kServicePath = "/org/freedesktop/secrets";
constexpr auto kServiceIface = "org.freedesktop.Secret.Service";
constexpr auto kCollectionIface = "org.freedesktop.Secret.Collection";
constexpr auto kItemIface = "org.freedesktop.Secret.Item";
constexpr auto kPromptIface = "org.freedesktop.Secret.Prompt";
constexpr auto kSessionIface = "org.freedesktop.Secret.Session";
constexpr auto kApp = "KoalaAmp";
constexpr int kCallMs = 4000;

struct DBusSecret {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument& operator<<(QDBusArgument& argument, const DBusSecret& secret) {
    argument.beginStructure();
    argument << secret.session << secret.parameters << secret.value << secret.contentType;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, DBusSecret& secret) {
    argument.beginStructure();
    argument >> secret.session >> secret.parameters >> secret.value >> secret.contentType;
    argument.endStructure();
    return argument;
}

using StringMap = QMap<QString, QString>;

void registerSecretTypes() {
    static const bool once = [] {
        qRegisterMetaType<DBusSecret>("koalaamp::servers::DBusSecret");
        qDBusRegisterMetaType<DBusSecret>();
        qDBusRegisterMetaType<StringMap>();
        qDBusRegisterMetaType<QList<QDBusObjectPath>>();
        return true;
    }();
    (void)once;
}

void note(const QString& text) {
    std::fprintf(stderr, "koalaamp: keyring: %s\n", qPrintable(text));
}

bool isReply(const QDBusMessage& reply) { return reply.type() == QDBusMessage::ReplyMessage; }

template <typename... Args>
QDBusMessage dbusCall(QDBusInterface& iface, const QString& method, Args&&... args) {
    iface.setTimeout(kCallMs);
    return iface.call(method, std::forward<Args>(args)...);
}

/// Blocks until the keyring's own dialog finishes. `path` "/" means no prompt.
class PromptWaiter : public QObject {
    Q_OBJECT
public:
    bool wait(const QString& path) {
        if (path.isEmpty() || path == QLatin1String("/")) {
            return true;
        }
        if (!QDBusConnection::sessionBus().connect(QString::fromLatin1(kServiceName), path,
                                                   QString::fromLatin1(kPromptIface),
                                                   QStringLiteral("Completed"), this,
                                                   SLOT(onCompleted(bool, QDBusVariant)))) {
            note(QStringLiteral("could not watch keyring prompt"));
            return false;
        }
        QDBusInterface prompt(QString::fromLatin1(kServiceName), path,
                              QString::fromLatin1(kPromptIface), QDBusConnection::sessionBus());
        prompt.call(QStringLiteral("Prompt"), QString());
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop_, &QEventLoop::quit);
        timer.start(30000);
        loop_.exec();
        return finished_ && !dismissed_;
    }

private slots:
    void onCompleted(bool dismissed, const QDBusVariant&) {
        dismissed_ = dismissed;
        finished_ = true;
        loop_.quit();
    }

private:
    QEventLoop loop_;
    bool dismissed_ = false;
    bool finished_ = false;
};

class SecretSession {
public:
    static std::optional<SecretSession> open() {
        registerSecretTypes();
        if (!QDBusConnection::sessionBus().isConnected()) {
            return std::nullopt;
        }
        QDBusMessage message = QDBusMessage::createMethodCall(
            QString::fromLatin1(kServiceName), QString::fromLatin1(kServicePath),
            QString::fromLatin1(kServiceIface), QStringLiteral("OpenSession"));
        message << QStringLiteral("plain") << QVariant::fromValue(QDBusVariant(QString()));
        const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, kCallMs);
        if (!isReply(reply) || reply.arguments().size() < 2) {
            return std::nullopt;
        }
        const auto path = reply.arguments().at(1).value<QDBusObjectPath>();
        if (path.path().isEmpty() || path.path() == QLatin1String("/")) {
            return std::nullopt;
        }
        return SecretSession(path);
    }

    ~SecretSession() {
        if (path_.path().isEmpty()) {
            return;
        }
        QDBusMessage message = QDBusMessage::createMethodCall(
            QString::fromLatin1(kServiceName), path_.path(), QString::fromLatin1(kSessionIface),
            QStringLiteral("Close"));
        QDBusConnection::sessionBus().call(message, QDBus::Block, kCallMs);
    }

    SecretSession(const SecretSession&) = delete;
    SecretSession& operator=(const SecretSession&) = delete;
    SecretSession(SecretSession&& other) noexcept : path_(std::move(other.path_)) {
        other.path_ = QDBusObjectPath();
    }
    SecretSession& operator=(SecretSession&&) = delete;

    const QDBusObjectPath& path() const { return path_; }

    QDBusObjectPath collection() const {
        QDBusInterface service(QString::fromLatin1(kServiceName), QString::fromLatin1(kServicePath),
                               QString::fromLatin1(kServiceIface), QDBusConnection::sessionBus());
        const QDBusReply<QDBusObjectPath> alias =
            dbusCall(service, QStringLiteral("ReadAlias"), QStringLiteral("default"));
        if (!alias.isValid()) {
            return QDBusObjectPath();
        }
        return alias.value();
    }

private:
    explicit SecretSession(QDBusObjectPath path) : path_(std::move(path)) {}
    QDBusObjectPath path_;
};

StringMap attributesFor(std::string_view key) {
    StringMap attributes;
    attributes.insert(QStringLiteral("application"), QString::fromLatin1(kApp));
    attributes.insert(QStringLiteral("server"), QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size())));
    return attributes;
}

QList<QDBusObjectPath> readPaths(const QVariant& value) {
    // The argument from a reply is read-only. The non-const beginArray() is the
    // writer, and calling it here logs "write from a read-only object" and
    // yields no paths.
    const QDBusArgument argument = value.value<QDBusArgument>();
    if (argument.currentType() != QDBusArgument::ArrayType) {
        return {};
    }
    QList<QDBusObjectPath> paths;
    argument.beginArray();
    while (!argument.atEnd()) {
        QDBusObjectPath path;
        argument >> path;
        paths.push_back(path);
    }
    argument.endArray();
    return paths;
}

bool dismissPrompt(const QDBusObjectPath& prompt) {
    PromptWaiter waiter;
    return waiter.wait(prompt.path());
}

QList<QDBusObjectPath> unlock(const QList<QDBusObjectPath>& locked) {
    if (locked.isEmpty()) {
        return {};
    }
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kServiceName), QString::fromLatin1(kServicePath),
        QString::fromLatin1(kServiceIface), QStringLiteral("Unlock"));
    message << QVariant::fromValue(locked);
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, kCallMs);
    if (!isReply(reply) || reply.arguments().size() < 2) {
        return {};
    }
    if (!dismissPrompt(reply.arguments().at(1).value<QDBusObjectPath>())) {
        return {};
    }
    return readPaths(reply.arguments().at(0));
}

struct Search {
    bool available = false;
    QList<QDBusObjectPath> items;
};

Search findItems(std::string_view key) {
    Search found;
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kServiceName), QString::fromLatin1(kServicePath),
        QString::fromLatin1(kServiceIface), QStringLiteral("SearchItems"));
    message << QVariant::fromValue(attributesFor(key));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, kCallMs);
    if (!isReply(reply)) {
        return found;
    }
    found.available = true;
    if (reply.arguments().size() < 2) {
        return found;
    }
    found.items = readPaths(reply.arguments().at(0));
    const auto locked = readPaths(reply.arguments().at(1));
    found.items += unlock(locked);
    return found;
}

std::optional<std::string> readItem(const SecretSession& session, const QDBusObjectPath& item) {
    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(kServiceName), item.path(),
                                                          QString::fromLatin1(kItemIface),
                                                          QStringLiteral("GetSecret"));
    message << session.path();
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, kCallMs);
    if (!isReply(reply) || reply.arguments().isEmpty()) {
        return std::nullopt;
    }
    DBusSecret secret;
    const QVariant body = reply.arguments().at(0);
    if (body.canConvert<QDBusArgument>()) {
        const QDBusArgument argument = body.value<QDBusArgument>();
        argument >> secret;
    } else if (body.canConvert<DBusSecret>()) {
        secret = body.value<DBusSecret>();
    } else {
        return std::nullopt;
    }
    if (secret.value.isEmpty()) {
        return std::nullopt;
    }
    return std::string(secret.value.constData(), static_cast<std::size_t>(secret.value.size()));
}

bool secretServiceStore(const SecretSession& session, std::string_view key, std::string_view secret) {
    const QDBusObjectPath collection = session.collection();
    if (collection.path().isEmpty() || collection.path() == QLatin1String("/")) {
        note(QStringLiteral("Secret Service has no default collection"));
        return false;
    }
    QVariantMap properties;
    properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Label"),
                      QStringLiteral("KoalaAmp Jellyfin"));
    properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Attributes"),
                      QVariant::fromValue(attributesFor(key)));

    DBusSecret value;
    value.session = session.path();
    value.value = QByteArray(secret.data(), static_cast<qsizetype>(secret.size()));
    value.contentType = QStringLiteral("text/plain");

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kServiceName), collection.path(), QString::fromLatin1(kCollectionIface),
        QStringLiteral("CreateItem"));
    message << properties << QVariant::fromValue(value) << true;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, kCallMs);
    if (!isReply(reply) || reply.arguments().size() < 2) {
        note(reply.errorMessage().isEmpty() ? QStringLiteral("CreateItem failed")
                                             : reply.errorMessage());
        return false;
    }
    if (!dismissPrompt(reply.arguments().at(1).value<QDBusObjectPath>())) {
        note(QStringLiteral("keyring prompt dismissed"));
        return false;
    }
    return true;
}

struct KwWallet {
    QString service;
    QString path;
    int handle = -1;
};

std::optional<KwWallet> openWallet() {
    const struct {
        const char* service;
        const char* path;
    } candidates[] = {
        {"org.kde.kwalletd6", "/modules/kwalletd6"},
        {"org.kde.kwalletd5", "/modules/kwalletd5"},
    };
    for (const auto& candidate : candidates) {
        QDBusInterface wallet(QString::fromLatin1(candidate.service), QString::fromLatin1(candidate.path),
                              QStringLiteral("org.kde.KWallet"), QDBusConnection::sessionBus());
        const QDBusReply<bool> enabled = dbusCall(wallet, QStringLiteral("isEnabled"));
        if (!enabled.isValid() || !enabled.value()) {
            continue;
        }
        const QDBusReply<QString> name = dbusCall(wallet, QStringLiteral("networkWallet"));
        if (!name.isValid() || name.value().isEmpty()) {
            continue;
        }
        const QDBusReply<int> handle =
            dbusCall(wallet, QStringLiteral("open"), name.value(), qlonglong{0}, QString::fromLatin1(kApp));
        if (!handle.isValid() || handle.value() < 0) {
            continue;
        }
        dbusCall(wallet, QStringLiteral("createFolder"), handle.value(), QString::fromLatin1(kApp),
                 QString::fromLatin1(kApp));
        return KwWallet{QString::fromLatin1(candidate.service), QString::fromLatin1(candidate.path),
                        handle.value()};
    }
    return std::nullopt;
}

QDBusInterface walletInterface(const KwWallet& wallet) {
    return QDBusInterface(wallet.service, wallet.path, QStringLiteral("org.kde.KWallet"),
                          QDBusConnection::sessionBus());
}

bool kwalletStore(std::string_view key, std::string_view secret) {
    const auto wallet = openWallet();
    if (!wallet) {
        return false;
    }
    auto iface = walletInterface(*wallet);
    const QDBusReply<int> wrote = dbusCall(
        iface, QStringLiteral("writePassword"), wallet->handle, QString::fromLatin1(kApp),
        QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size())),
        QString::fromUtf8(secret.data(), static_cast<qsizetype>(secret.size())),
        QString::fromLatin1(kApp));
    return wrote.isValid() && wrote.value() == 0;
}

std::optional<std::string> kwalletLoad(std::string_view key) {
    const auto wallet = openWallet();
    if (!wallet) {
        return std::nullopt;
    }
    auto iface = walletInterface(*wallet);
    const QDBusReply<QString> value = dbusCall(
        iface, QStringLiteral("readPassword"), wallet->handle, QString::fromLatin1(kApp),
        QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size())), QString::fromLatin1(kApp));
    if (!value.isValid() || value.value().isEmpty()) {
        return std::nullopt;
    }
    return value.value().toStdString();
}

bool kwalletRemove(std::string_view key) {
    const auto wallet = openWallet();
    if (!wallet) {
        return false;
    }
    auto iface = walletInterface(*wallet);
    const QDBusReply<int> removed = dbusCall(
        iface, QStringLiteral("removeEntry"), wallet->handle, QString::fromLatin1(kApp),
        QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size())), QString::fromLatin1(kApp));
    return removed.isValid();
}

class LinuxSecretStore final : public SecretStore {
public:
    bool store(std::string_view key, std::string_view secret) override {
        if (const auto session = SecretSession::open()) {
            return secretServiceStore(*session, key, secret);
        }
        return kwalletStore(key, secret);
    }

    std::optional<std::string> load(std::string_view key) const override {
        if (const auto session = SecretSession::open()) {
            const Search found = findItems(key);
            if (!found.available) {
                return kwalletLoad(key);
            }
            if (found.items.isEmpty()) {
                return std::nullopt;
            }
            return readItem(*session, found.items.front());
        }
        return kwalletLoad(key);
    }

    bool remove(std::string_view key) override {
        bool removed = false;
        if (const auto session = SecretSession::open()) {
            const Search found = findItems(key);
            if (found.available) {
                for (const auto& item : found.items) {
                    QDBusInterface iface(QString::fromLatin1(kServiceName), item.path(),
                                         QString::fromLatin1(kItemIface), QDBusConnection::sessionBus());
                    const QDBusReply<QDBusObjectPath> prompt = dbusCall(iface, QStringLiteral("Delete"));
                    if (prompt.isValid() && dismissPrompt(prompt.value())) {
                        removed = true;
                    }
                }
                return removed || found.items.isEmpty();
            }
        }
        return kwalletRemove(key);
    }
};

} // namespace

std::unique_ptr<SecretStore> makePlatformSecretStore() {
    return std::make_unique<LinuxSecretStore>();
}

} // namespace koalaamp::servers

#include "PlatformSecretStore_linux.moc"
