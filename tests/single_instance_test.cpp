// SPDX-License-Identifier: MIT
// CO-1 — the per-user single-instance socket forwards argv to the primary.
//
// The forwarding side runs in a child process, exactly as a desktop launch does.
// That avoids re-entrant event-loop tricks in one process (and the Windows
// named-pipe failure they exposed) while still being a normal ctest case.

#include "platform/SingleInstance.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QProcess>
#include <QTimer>
#include <QUuid>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>

namespace {

using koalaamp::platform::SingleInstance;

QString uniqueName() {
    return QStringLiteral("koalaamp-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace

TEST_CASE("single instance forwards argv to the primary") {
    const QString name = uniqueName();
    SingleInstance primary(name);
    REQUIRE(primary.start() == SingleInstance::Role::Primary);
    REQUIRE(primary.isListening());
    std::fprintf(stderr, "single-instance test: primary listening\n");

    QStringList received;
    QEventLoop loop;
    QObject::connect(&primary, &SingleInstance::argumentsReceived, &primary,
                     [&](const QStringList& args) {
                         received = args;
                         loop.quit();
                     });

    QProcess child;
    child.setProgram(QCoreApplication::applicationFilePath());
    child.setArguments({QStringLiteral("--forward"), name, QStringLiteral("--enqueue"),
                        QStringLiteral("/music/a.flac")});
    child.start();
    std::fprintf(stderr, "single-instance test: child started\n");
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    std::fprintf(stderr, "single-instance test: loop returned (%d args)\n", received.size());

    REQUIRE(received.size() == 2);
    CHECK(received[0] == "--enqueue");
    CHECK(received[1] == "/music/a.flac");
    CHECK(child.waitForFinished(1000));
    CHECK(child.exitStatus() == QProcess::NormalExit);
    CHECK(child.exitCode() == 0);
    CHECK(primary.isListening());
    std::fprintf(stderr, "single-instance test: checks done\n");
}

TEST_CASE("a failed forward becomes the primary") {
    SingleInstance instance(uniqueName());
    CHECK(instance.start() == SingleInstance::Role::Primary);
    CHECK(instance.isListening());
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc == 5 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--forward")) {
        SingleInstance forwarder(QString::fromLocal8Bit(argv[2]));
        const auto role =
            forwarder.start({QString::fromLocal8Bit(argv[3]), QString::fromLocal8Bit(argv[4])});
        return role == SingleInstance::Role::Forwarded ? 0 : 2;
    }
    return Catch::Session().run(argc, argv);
}
