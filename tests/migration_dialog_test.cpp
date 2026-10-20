// SPDX-License-Identifier: MIT
// MI-1 — the three-step Winamp import dialog. The NDE reader itself is covered
// by migration_tests; this pins the wizard's path validation, option routing and
// report, with the playlist/preset effects delegated to host callbacks.

#include "ui/MigrationDialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using koalaamp::ui::MigrationDialog;

struct Fixture {
    fs::path dir;

    Fixture() {
        dir = fs::temp_directory_path() / "koalaamp-migration-dialog-test";
        fs::remove_all(dir);
        fs::create_directories(dir);
        std::ofstream(dir / "winamp.ini") << "[Winamp]\nconfig_volume=180\n";
        std::ofstream(dir / "winamp.m3u") << "#EXTM3U\n" << (dir / "track.flac").string() << "\n";
        fs::create_directories(dir / "plugins" / "ml" / "playlists");
        fs::create_directories(dir / "Skins");
        std::ofstream(dir / "Skins" / "test.wsz") << "not-a-real-skin";
        std::ofstream(dir / "plugins" / "ml" / "playlists" / "playlists.xml")
            << "<?xml version=\"1.0\"?><Winamp:Playlists>"
               "<playlist filename=\"mix.m3u\" title=\"Mix\" songs=\"1\" seconds=\"1\" "
               "id=\"{1}\"/></Winamp:Playlists>";
    }
    ~Fixture() {
        std::error_code error;
        fs::remove_all(dir, error);
    }
};

template <typename Widget>
Widget* child(QWidget& dialog, const char* name) {
    auto* widget = dialog.findChild<Widget*>(QString::fromLatin1(name));
    REQUIRE(widget != nullptr);
    return widget;
}

} // namespace

TEST_CASE("migration wizard validates a folder and routes the selected imports") {
    Fixture fixture;
    std::string playlistPath;
    std::string indexPath;
    std::string skinsPath;
    int presetCalls = 0;
    MigrationDialog dialog((fixture.dir / "library.db").string(),
                           [&](const std::string& path) {
                               playlistPath = path;
                               return true;
                           },
                           [&](const std::string&) {
                               ++presetCalls;
                               return true;
                           });
    dialog.show();
    dialog.setIndexImporter([&](const std::string& path) {
        indexPath = path;
        return true;
    });
    dialog.setSkinImporter([&](const std::string& path) {
        skinsPath = path;
        return true;
    });

    child<QLineEdit>(dialog, "winampFolder")->setText(QString::fromStdString(fixture.dir.string()));
    child<QPushButton>(dialog, "winampNext")->click();

    auto* ratings = child<QCheckBox>(dialog, "winampRatings");
    auto* playlist = child<QCheckBox>(dialog, "winampPlaylist");
    auto* presets = child<QCheckBox>(dialog, "winampPresets");
    auto* mlPlaylists = child<QCheckBox>(dialog, "winampMlPlaylists");
    auto* skins = child<QCheckBox>(dialog, "winampSkins");
    CHECK(ratings->isVisible());
    CHECK(playlist->isVisible());
    CHECK(presets->isVisible());
    CHECK(mlPlaylists->isVisible());
    CHECK(skins->isVisible());

    // The NDE path is already exercised end to end in migration_tests; here the
    // wizard must still run with ratings off and route the two file imports.
    ratings->setChecked(false);
    presets->setChecked(false);
    child<QPushButton>(dialog, "winampImport")->click();

    CHECK(playlistPath == (fixture.dir / "winamp.m3u").string());
    CHECK(indexPath == (fixture.dir / "plugins" / "ml" / "playlists" / "playlists.xml").string());
    CHECK(presetCalls == 0);
    auto* report = child<QLabel>(dialog, "winampReport");
    CHECK(report->isVisible());
    CHECK(report->text().contains(QStringLiteral("Ratings: not selected")));
    CHECK(report->text().contains(QStringLiteral("Playlist: imported")));
    CHECK(report->text().contains(QStringLiteral("ML playlists: imported")));
    CHECK(report->text().contains(QStringLiteral("Skins: copied")));
    CHECK(child<QPushButton>(dialog, "winampClose")->isVisible());
}

TEST_CASE("migration wizard rejects a folder without Winamp data") {
    Fixture fixture;
    const fs::path empty = fixture.dir / "empty";
    fs::create_directories(empty);
    MigrationDialog dialog((fixture.dir / "library.db").string(), nullptr, nullptr);
    child<QLineEdit>(dialog, "winampFolder")->setText(QString::fromStdString(empty.string()));
    child<QPushButton>(dialog, "winampNext")->click();
    auto* status = child<QLabel>(dialog, "winampStatus");
    CHECK(status->text().contains(QStringLiteral("No winamp.ini")));
    CHECK(child<QPushButton>(dialog, "winampImport")->isVisible() == false);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
