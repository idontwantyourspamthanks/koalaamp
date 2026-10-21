// SPDX-License-Identifier: MIT
// platform — see SingleInstance.hpp.

#include "platform/SingleInstance.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QThread>

namespace koalaamp::platform {

SingleInstance::SingleInstance(QString name, QObject* parent)
    : QObject(parent), m_name(std::move(name)) {}

SingleInstance::~SingleInstance() = default;

bool SingleInstance::tryForward(const QStringList& args, int timeoutMs) {
    QLocalSocket socket;
    socket.connectToServer(m_name);

    // Pump events while connecting: a same-process primary accepts on its own
    // event loop, and Windows named-pipe servers need that loop to service the
    // connection. A real secondary has no primary event loop to service, so
    // this is also harmless there.
    QElapsedTimer timer;
    timer.start();
    while (socket.state() == QLocalSocket::ConnectingState && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    if (socket.state() != QLocalSocket::ConnectedState) {
        return false;
    }

    QJsonArray array;
    for (const QString& arg : args) {
        array.append(arg);
    }
    const QByteArray payload =
        QJsonDocument(array).toJson(QJsonDocument::Compact) + QByteArrayLiteral("\n");
    socket.write(payload);
    while (socket.bytesToWrite() > 0 && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    const bool sent = socket.bytesToWrite() == 0;
    socket.disconnectFromServer();
    return sent;
}

SingleInstance::Role SingleInstance::start(const QStringList& args, int timeoutMs) {
    // An already-running primary answers before we touch its socket file.
    if (tryForward(args, timeoutMs)) {
        return Role::Forwarded;
    }

    QLocalServer::removeServer(m_name);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(m_name)) {
        // Lost a startup race after the stale-socket check: try the winner.
        if (tryForward(args, timeoutMs)) {
            return Role::Forwarded;
        }
        return Role::Failed;
    }
    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstance::acceptConnection);
    m_primary = true;
    return Role::Primary;
}

void SingleInstance::acceptConnection() {
    while (m_server.hasPendingConnections()) {
        QLocalSocket* socket = m_server.nextPendingConnection();
        if (socket == nullptr) {
            continue;
        }
        m_buffers.insert(socket, QByteArray{});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readClient(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] { dropClient(socket); });
        if (socket->bytesAvailable() > 0) {
            readClient(socket);
        }
    }
}

void SingleInstance::readClient(QLocalSocket* socket) {
    if (socket == nullptr) {
        return;
    }
    QByteArray& buffer = m_buffers[socket];
    buffer += socket->readAll();
    while (true) {
        const qsizetype newline = buffer.indexOf('\n');
        if (newline < 0) {
            break;
        }
        const QByteArray line = buffer.left(newline);
        buffer.remove(0, newline + 1);
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isArray()) {
            continue;
        }
        QStringList args;
        const QJsonArray array = document.array();
        args.reserve(array.size());
        for (const auto& value : array) {
            if (value.isString()) {
                args.append(value.toString());
            }
        }
        emit argumentsReceived(args);
    }
}

void SingleInstance::dropClient(QLocalSocket* socket) {
    if (socket == nullptr) {
        return;
    }
    m_buffers.remove(socket);
    socket->deleteLater();
}

} // namespace koalaamp::platform
