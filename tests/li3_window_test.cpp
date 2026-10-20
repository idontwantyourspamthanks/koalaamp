// SPDX-License-Identifier: MIT
// LI-3 — offscreen library-window render smoke: fallback chrome plus the first
// five corpus skins. The window is constructed against a temporary DB with a
// handful of rows, then rendered at its fixed gen size.

#include "library/LibraryDb.hpp"
#include "library/Scanner.hpp"
#include "skins/SkinSet.hpp"
#include "ui/GenRenderer.hpp"
#include "ui/LibraryWindow.hpp"

#include <QApplication>
#include <QImage>
#include <QPixmap>
#include <QStandardPaths>
#include <QTableWidget>
#include <algorithm>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace {

using koalaamp::ui::LibraryWindow;

namespace fs = std::filesystem;

std::vector<fs::path> corpusSkins() {
    std::vector<fs::path> paths;
    const fs::path dir = KOALAAMP_CORPUS_DIR;
    if (!fs::is_directory(dir)) {
        return paths;
    }
    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::string ext = entry.path().extension().string();
        if (ext == ".wsz" || ext == ".zip") {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::size_t distinctColours(const QImage& image) {
    std::set<QRgb> colours;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            colours.insert(image.pixel(x, y));
        }
    }
    return colours.size();
}

QImage render(LibraryWindow& window) {
    QPixmap pixmap(window.size());
    pixmap.fill(Qt::transparent);
    window.render(&pixmap);
    return pixmap.toImage();
}

} // namespace

TEST_CASE("LI-3 library window renders its fallback and five corpus skins",
          "[li3][window][corpus]") {
    const fs::path dir = fs::temp_directory_path() / "koalaamp-li3-window-test";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir, error);
    const fs::path dbPath = dir / "library.db";

    {
        koalaamp::library::LibraryDb db(dbPath);
        REQUIRE(db.open());
        for (int i = 0; i < 5; ++i) {
            koalaamp::library::ScannedTrack scanned;
            scanned.track.path = (dir / (std::to_string(i) + ".flac")).string();
            scanned.track.title = "Title " + std::to_string(i);
            scanned.track.artist = "Artist " + std::to_string(i % 2);
            scanned.track.album = "Album";
            scanned.track.albumArtist = scanned.track.artist;
            scanned.track.durationMs = 180000;
            scanned.format = "flac";
            scanned.mtime = i + 1;
            scanned.size = 100;
            REQUIRE(db.upsert(scanned));
        }
    }

    LibraryWindow window(dbPath.string(), [](const auto&, std::size_t) {});
    window.show();
    QApplication::processEvents();
    auto* table = window.findChild<QTableWidget*>(QStringLiteral("libraryTracks"));
    REQUIRE(table != nullptr);
    CHECK(table->rowCount() == 5);

    const QImage fallback = render(window);
    CHECK(fallback.size() == window.size());
    CHECK(distinctColours(fallback) > 2);

    const std::vector<fs::path> skins = corpusSkins();
    if (skins.empty()) {
        SKIP("corpus missing — run scripts/fetch-corpus.sh");
    }
    INFO("corpus skins: " << skins.size());
    std::size_t withGen = 0;
    bool withoutGenTested = false;
    for (const fs::path& path : skins) {
        INFO("skin: " << path.filename().string());
        const auto skin = koalaamp::skins::loadSkinSet(path);
        REQUIRE(skin.has_value());
        REQUIRE(skin->isComplete());
        const bool hasGen = skin->sheets.sheet("GEN") != nullptr;
        if (hasGen && withGen < 5) {
            window.setSkin(&*skin);
            const QImage frame = render(window);
            CHECK(frame.size() == window.size());
            CHECK(distinctColours(frame) > 2);
            ++withGen;
        }
        if (!hasGen && !withoutGenTested) {
            CHECK(koalaamp::ui::genUsesFallback(skin->sheets));
            window.setSkin(&*skin);
            const QImage frame = render(window);
            CHECK(frame.size() == window.size());
            CHECK(distinctColours(frame) > 2);
            withoutGenTested = true;
        }
        if (withGen >= 5 && withoutGenTested) {
            break;
        }
    }
    CHECK(withGen == 5);
    CHECK(withoutGenTested);
    window.setSkin(nullptr);
    CHECK(render(window).size() == window.size());

    window.close();
    fs::remove_all(dir, error);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("KoalaAmpLi3Tests"));
    QCoreApplication::setOrganizationName(QStringLiteral("KoalaAmpTests"));
    QStandardPaths::setTestModeEnabled(true);
    return Catch::Session().run(argc, argv);
}
