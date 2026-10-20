// SPDX-License-Identifier: MIT
// SV-5 — the server browser window against the fixture transport: library
// filtering, album/playlist loading, track loading and the play callback.

#include "servers/jellyfin/FixtureTransport.hpp"
#include "servers/ServerBook.hpp"
#include "ui/ConnectionDialog.hpp"
#include "ui/ServerBrowserWindow.hpp"

#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTest>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <system_error>
#include <string>
#include <vector>

namespace {

using namespace koalaamp::servers;
using namespace koalaamp::servers::jellyfin;
using koalaamp::library::Track;
using koalaamp::ui::ServerBrowserWindow;
using koalaamp::ui::ConnectionDialog;
using koalaamp::servers::ServerBook;

constexpr const char* kFixtureDir = KOALAAMP_FIXTURE_DIR;

void activate(QListWidget* list, int row) {
    QListWidgetItem* item = list->item(row);
    REQUIRE(item != nullptr);
    QMetaObject::invokeMethod(list, "itemActivated", Qt::DirectConnection,
                              Q_ARG(QListWidgetItem*, item));
}

} // namespace

TEST_CASE("the browser loads libraries, albums, playlists and plays tracks") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    const ServerAuth auth{"http://jellyfin.local:8096", "user-1", "fixture-token-1",
                          "device-under-test"};

    std::vector<Track> played;
    std::size_t startedAt = 999;
    bool enqueued = true;
    ServerBrowserWindow window{transport, auth,
                               [&](const std::vector<Track>& tracks, std::size_t start,
                                   bool enqueueNext) {
                                   played = tracks;
                                   startedAt = start;
                                   enqueued = enqueueNext;
                               }};

    auto* libraries = window.findChild<QListWidget*>(QStringLiteral("libraryList"));
    auto* albums = window.findChild<QListWidget*>(QStringLiteral("albumList"));
    auto* playlists = window.findChild<QListWidget*>(QStringLiteral("playlistList"));
    auto* tracks = window.findChild<QListWidget*>(QStringLiteral("trackList"));
    REQUIRE(libraries != nullptr);
    REQUIRE(albums != nullptr);
    REQUIRE(playlists != nullptr);
    REQUIRE(tracks != nullptr);

    // The views fixture has music + movies; only the music view is listed.
    REQUIRE(libraries->count() == 1);
    CHECK(libraries->item(0)->text() == "Music");

    // The first music library loads automatically.
    REQUIRE(albums->count() == 2);
    CHECK(albums->item(0)->text() == "First Album");

    REQUIRE(playlists->count() == 2);
    CHECK(playlists->item(0)->text() == "Road Trip");

    activate(albums, 0);
    REQUIRE(tracks->count() == 2);
    CHECK(tracks->item(0)->text() == "First — Artist");

    activate(tracks, 1);
    REQUIRE(played.size() == 2);
    CHECK(startedAt == 1);
    CHECK_FALSE(enqueued);
    CHECK(played[0].id == "track-1");
    CHECK(played[1].id == "track-2");
}

TEST_CASE("selecting a playlist loads its items into the track column") {
    FixtureTransport transport{std::filesystem::path(kFixtureDir)};
    const ServerAuth auth{"http://jellyfin.local:8096", "user-1", "fixture-token-1",
                          "device-under-test"};
    ServerBrowserWindow window{transport, auth, [](const std::vector<Track>&, std::size_t, bool) {}};

    auto* playlists = window.findChild<QListWidget*>(QStringLiteral("playlistList"));
    auto* albums = window.findChild<QListWidget*>(QStringLiteral("albumList"));
    auto* tracks = window.findChild<QListWidget*>(QStringLiteral("trackList"));
    REQUIRE(playlists != nullptr);
    REQUIRE(albums != nullptr);
    REQUIRE(tracks != nullptr);
    REQUIRE(playlists->count() == 2);

    activate(playlists, 0);
    CHECK(albums->count() == 0); // playlist mode clears the album column
    REQUIRE(tracks->count() == 2);
    CHECK(tracks->item(0)->text() == "Highway — Artist");
    CHECK(tracks->item(1)->text() == "Sunset — Artist");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("the connection dialog offers Browse once signed in") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "koalaamp-dialog-browse-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    FixtureTransport transport{fs::path(kFixtureDir)};
    ServerBook book{(dir / "servers.json").string()};
    ConnectionDialog dialog{transport, book, (dir / "machine-id").string()};

    auto* url = dialog.findChild<QLineEdit*>(QStringLiteral("urlField"));
    auto* user = dialog.findChild<QLineEdit*>(QStringLiteral("userField"));
    auto* password = dialog.findChild<QLineEdit*>(QStringLiteral("passwordField"));
    auto* login = dialog.findChild<QPushButton*>(QStringLiteral("loginButton"));
    auto* browse = dialog.findChild<QPushButton*>(QStringLiteral("browseButton"));
    REQUIRE(url != nullptr);
    REQUIRE(user != nullptr);
    REQUIRE(password != nullptr);
    REQUIRE(login != nullptr);
    REQUIRE(browse != nullptr);
    CHECK_FALSE(browse->isEnabled());

    bool requested = false;
    QObject::connect(&dialog, &ConnectionDialog::browseRequested,
                     [&] { requested = true; });

    url->setText(QStringLiteral("http://jellyfin.local:8096"));
    user->setText(QStringLiteral("tester"));
    password->setText(QStringLiteral("password"));
    login->click();

    REQUIRE(dialog.session().has_value());
    CHECK(browse->isEnabled());
    browse->click();
    CHECK(requested);

    // Restart model: a fresh book from the same file has the identity but no
    // token. Selecting the saved server prefills URL/user; only the password is
    // entered again, and signing in re-enables Browse.
    FixtureTransport restartedTransport{fs::path(kFixtureDir)};
    ServerBook reopened{(dir / "servers.json").string()};
    REQUIRE(reopened.load());
    ConnectionDialog restarted{restartedTransport, reopened, (dir / "machine-id").string()};
    auto* url2 = restarted.findChild<QLineEdit*>(QStringLiteral("urlField"));
    auto* user2 = restarted.findChild<QLineEdit*>(QStringLiteral("userField"));
    auto* password2 = restarted.findChild<QLineEdit*>(QStringLiteral("passwordField"));
    auto* login2 = restarted.findChild<QPushButton*>(QStringLiteral("loginButton"));
    auto* browse2 = restarted.findChild<QPushButton*>(QStringLiteral("browseButton"));
    REQUIRE(url2 != nullptr);
    REQUIRE(user2 != nullptr);
    REQUIRE(password2 != nullptr);
    REQUIRE(login2 != nullptr);
    REQUIRE(browse2 != nullptr);
    CHECK(url2->text() == QStringLiteral("http://jellyfin.local:8096"));
    CHECK(user2->text() == QStringLiteral("tester"));
    CHECK(password2->text().isEmpty());
    CHECK_FALSE(restarted.session().has_value());
    CHECK_FALSE(browse2->isEnabled());

    password2->setText(QStringLiteral("password"));
    login2->click();
    CHECK(restarted.session().has_value());
    CHECK(browse2->isEnabled());

    fs::remove_all(dir, ec);
}
