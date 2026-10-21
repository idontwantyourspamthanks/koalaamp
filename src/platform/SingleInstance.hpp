// SPDX-License-Identifier: MIT
// platform — per-user single-instance IPC for CLI/file-association verbs.
//
// The first process listens on a named local socket; later launches connect,
// forward their argv as one JSON array, and exit. A stale socket is removed
// only after a forward attempt fails, so an active primary is never unlinked.

#pragma once

#include <QByteArray>
#include <QHash>
#include <QLocalServer>
#include <QObject>
#include <QString>
#include <QStringList>

class QLocalSocket;

namespace koalaamp::platform {

class SingleInstance final : public QObject {
    Q_OBJECT

public:
    enum class Role { Primary, Forwarded, Failed };

    explicit SingleInstance(QString name, QObject* parent = nullptr);
    ~SingleInstance() override;

    /// Tries to forward `args` to an existing instance first. If none answers,
    /// becomes the primary and listens for later launches.
    Role start(const QStringList& args = {}, int timeoutMs = 500);

    const QString& name() const { return m_name; }
    bool isListening() const { return m_server.isListening(); }

signals:
    void argumentsReceived(const QStringList& args);

private:
    void acceptConnection();
    void readClient(QLocalSocket* socket);
    void dropClient(QLocalSocket* socket);
    bool tryForward(const QStringList& args, int timeoutMs);

    QString m_name;
    bool m_primary{false};
    QLocalServer m_server;
    QHash<QLocalSocket*, QByteArray> m_buffers;
};

} // namespace koalaamp::platform
