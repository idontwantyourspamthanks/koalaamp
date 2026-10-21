// SPDX-License-Identifier: MIT
// CO-1 — the per-user single-instance socket forwards arguments to the primary.

#include "platform/SingleInstance.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QUuid>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

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

    QStringList received;
    QEventLoop loop;
    QObject::connect(&primary, &SingleInstance::argumentsReceived, &primary,
                     [&](const QStringList& args) {
                         received = args;
                         loop.quit();
                     });

    QTimer::singleShot(0, &loop, [&] {
        SingleInstance secondary(name);
        CHECK(secondary.start({QStringLiteral("--enqueue"), QStringLiteral("/music/a.flac")}) ==
              SingleInstance::Role::Forwarded);
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    REQUIRE(received.size() == 2);
    CHECK(received[0] == "--enqueue");
    CHECK(received[1] == "/music/a.flac");
    CHECK(primary.isListening());
}

TEST_CASE("a failed forward becomes the primary") {
    SingleInstance instance(uniqueName());
    CHECK(instance.start() == SingleInstance::Role::Primary);
    CHECK(instance.isListening());
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
