// SPDX-License-Identifier: MIT
// SV-5 — the server browser window against the fixture transport: library
// filtering, album/playlist loading, track loading and the play callback.

#include "servers/jellyfin/FixtureTransport.hpp"
#include "ui/ServerBrowserWindow.hpp"

#include <QApplication>
#include <QListWidget>
#include <QTest>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace koalaamp::servers;
using namespace koalaamp::servers::jellyfin;
using koalaamp::library::Track;
using koalaamp::ui::ServerBrowserWindow;

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
