// SPDX-License-Identifier: MIT
// LI-3 — offscreen library-window render smoke: fallback chrome plus the first
// five corpus skins. The window is constructed against a temporary DB with a
// handful of rows, then rendered at its fixed gen size.

#include "library/LibraryDb.hpp"
#include "library/Scanner.hpp"
#include "skins/Skin.hpp"
#include "skins/SkinSet.hpp"
#include "ui/GenRenderer.hpp"
#include "ui/GenexStyle.hpp"
#include "ui/LibraryWindow.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QPixmap>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTreeWidget>
#include <algorithm>
#include <array>
#include <catch2/catch_message.hpp>
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

void saveIfRequested(const QImage& image, const std::string& name) {
    const QByteArray directory = qgetenv("KOALAAMP_LI3_SCREENSHOT");
    if (directory.isEmpty()) {
        return;
    }
    QDir().mkpath(QString::fromLocal8Bit(directory));
    image.save(QDir(QString::fromLocal8Bit(directory)).filePath(QString::fromStdString(name)));
}

} // namespace

TEST_CASE("genex swatches map to the documented widget roles", "[li3][genex]") {
    koalaamp::skins::Skin skin;
    QImage sheet(130, 75, QImage::Format_RGB32);
    sheet.fill(QColor(0, 198, 255));
    const std::array<QColor, 18> colours = {
        QColor(10, 11, 12),    QColor(20, 21, 22),    QColor(30, 31, 32),    QColor(40, 41, 42),
        QColor(50, 51, 52),    QColor(60, 61, 62),    QColor(70, 71, 72),    QColor(80, 81, 82),
        QColor(90, 91, 92),    QColor(100, 101, 102), QColor(110, 111, 112), QColor(120, 121, 122),
        QColor(130, 131, 132), QColor(140, 141, 142), QColor(150, 151, 152), QColor(160, 161, 162),
        QColor(170, 171, 172), QColor(180, 181, 182),
    };
    for (std::size_t i = 0; i < colours.size(); ++i) {
        sheet.setPixel(48 + static_cast<int>(i) * 2, 0, colours[i].rgb());
    }
    skin.addSheet("GENEX", sheet);

    const koalaamp::ui::GenexPalette palette = koalaamp::ui::genexPalette(&skin);
    CHECK(palette.valid);
    CHECK(palette.itemBackground == colours[0]);
    CHECK(palette.itemText == colours[1]);
    CHECK(palette.windowBackground == colours[2]);
    CHECK(palette.buttonText == colours[3]);
    CHECK(palette.windowText == colours[4]);
    CHECK(palette.divider == colours[5]);
    CHECK(palette.selection == colours[6]);
    CHECK(palette.headerBackground == colours[7]);
    CHECK(palette.headerText == colours[8]);
    CHECK(palette.headerTopLeft == colours[9]);
    CHECK(palette.headerBottomRight == colours[10]);
    CHECK(palette.headerPressed == colours[11]);
    CHECK(palette.headerDead == colours[12]);
    CHECK(palette.scrollbar1 == colours[13]);
    CHECK(palette.scrollbar2 == colours[14]);
    CHECK(palette.pressedScrollbar1 == colours[15]);
    CHECK(palette.pressedScrollbar2 == colours[16]);
    CHECK(palette.scrollbarDead == colours[17]);

    CHECK(koalaamp::ui::genexHasArt(&skin));
    const auto button = skin.spriteRect("GENEX", "GENEX_BUTTON_ACTIVE");
    REQUIRE(button.has_value());
    CHECK(button->x == 0);
    CHECK(button->y == 0);
    CHECK(button->w == 47);
    CHECK(button->h == 15);
    const auto pressed = skin.spriteRect("GENEX", "GENEX_BUTTON_PRESSED");
    REQUIRE(pressed.has_value());
    CHECK(pressed->x == 0);
    CHECK(pressed->y == 15);
    const auto scrollDown = skin.spriteRect("GENEX", "GENEX_SCROLL_DOWN");
    REQUIRE(scrollDown.has_value());
    CHECK(scrollDown->x == 14);
    CHECK(scrollDown->y == 31);
    const auto slider = skin.spriteRect("GENEX", "GENEX_VSLIDER");
    REQUIRE(slider.has_value());
    CHECK(slider->x == 56);
    CHECK(slider->y == 31);
    CHECK(slider->w == 14);
    CHECK(slider->h == 28);
    const auto horizontal = skin.spriteRect("GENEX", "GENEX_HSLIDER_PRESSED");
    REQUIRE(horizontal.has_value());
    CHECK(horizontal->x == 84);
    CHECK(horizontal->y == 45);
    CHECK(horizontal->w == 28);
    CHECK(horizontal->h == 14);
    CHECK_FALSE(koalaamp::ui::genexPalette(nullptr).valid);
}

TEST_CASE("genex scrollbar arrows keep native stepping", "[li3][genex]") {
    koalaamp::ui::GenexScrollBar bar(Qt::Vertical);
    bar.setRange(0, 100);
    bar.setPageStep(10);
    bar.resize(14, 200);
    bar.show();
    QApplication::processEvents();

    const auto press = [&bar](const QPoint& point) {
        QMouseEvent event(QEvent::MouseButtonPress, QPointF(point), QPointF(point), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&bar, &event);
    };
    const auto release = [&bar](const QPoint& point) {
        QMouseEvent event(QEvent::MouseButtonRelease, QPointF(point), QPointF(point),
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&bar, &event);
    };

    bar.setValue(50);
    press(QPoint(7, 7));
    QApplication::processEvents();
    CHECK(bar.value() == 50 - bar.singleStep());
    release(QPoint(7, 7));

    bar.setValue(50);
    press(QPoint(7, 193));
    QApplication::processEvents();
    CHECK(bar.value() == 50 + bar.singleStep());
    release(QPoint(7, 193));
    bar.close();
}

TEST_CASE("gen windows ignore the classic region mask", "[li3][genex]") {
    const fs::path dir = fs::temp_directory_path() / "koalaamp-li3-region-test";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir, error);
    const fs::path dbPath = dir / "library.db";
    {
        koalaamp::library::LibraryDb db(dbPath);
        REQUIRE(db.open());
    }

    koalaamp::skins::SkinSet synthetic;
    synthetic.sheets.addSheet("MAIN", QImage(275, 116, QImage::Format_RGB32));
    synthetic.region =
        koalaamp::skins::parseRegion("[Normal]\nNumPoints=4\nPointList=0,0,100,0,100,100,0,100\n");
    REQUIRE_FALSE(synthetic.region.empty());

    LibraryWindow window(dbPath.string(), [](const auto&, std::size_t) {});
    window.setSkin(&synthetic);
    CHECK(window.mask().isEmpty());
    window.close();
    fs::remove_all(dir, error);
}

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
        koalaamp::library::PlayHistoryEntry history;
        history.path = (dir / "0.flac").string();
        history.title = "Title 0";
        history.artist = "Artist 0";
        history.album = "Album";
        history.playedAt = 1'700'000'000;
        REQUIRE(db.addHistory(history));
    }

    LibraryWindow window(dbPath.string(), [](const auto&, std::size_t) {});
    window.show();
    QApplication::processEvents();
    auto* table = window.findChild<QTableWidget*>(QStringLiteral("libraryTracks"));
    REQUIRE(table != nullptr);
    CHECK(table->rowCount() == 5);
    auto* vertical = window.findChild<QScrollBar*>(QStringLiteral("libraryTracksScrollVertical"));
    REQUIRE(vertical != nullptr);
    CHECK(vertical->width() == 14);

    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("libraryTree"));
    REQUIRE(tree != nullptr);
    QTreeWidgetItem* historyItem = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(0) == QStringLiteral("History")) {
            historyItem = tree->topLevelItem(i);
            break;
        }
    }
    REQUIRE(historyItem != nullptr);
    tree->setCurrentItem(historyItem);
    QApplication::processEvents();
    CHECK(table->rowCount() == 1);
    CHECK(table->item(0, 0)->text() == QStringLiteral("Title 0"));

    const QImage fallback = render(window);
    CHECK(fallback.size() == window.size());
    CHECK(distinctColours(fallback) > 2);
    saveIfRequested(fallback, "fallback.png");

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
            saveIfRequested(frame, path.stem().string() + ".png");
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

TEST_CASE("LI-3 100k-row scroll frame budget", "[.bench][li3]") {
    const fs::path dir = fs::temp_directory_path() / "koalaamp-li3-scroll-bench";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir, error);
    const fs::path dbPath = dir / "library.db";
    {
        koalaamp::library::LibraryDb db(dbPath);
        REQUIRE(db.open());
    }

    LibraryWindow window(dbPath.string(), [](const auto&, std::size_t) {});
    window.show();
    QApplication::processEvents();
    auto* table = window.findChild<QTableWidget*>(QStringLiteral("libraryTracks"));
    REQUIRE(table != nullptr);
    auto* scroll = table->verticalScrollBar();
    REQUIRE(scroll != nullptr);

    table->setSortingEnabled(false);
    table->setRowCount(100000);
    for (int row = 0; row < 100000; ++row) {
        const QString text = QStringLiteral("Track %1").arg(row);
        table->setItem(row, 0, new QTableWidgetItem(text));
        table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("3:00")));
        table->setItem(row, 2, new QTableWidgetItem(QStringLiteral("Artist %1").arg(row % 100)));
    }
    table->setSortingEnabled(true);
    QApplication::processEvents();

    constexpr int kFrames = 240;
    std::vector<double> frameMillis;
    frameMillis.reserve(kFrames);
    QPixmap target(window.size());
    for (int frame = 0; frame < kFrames; ++frame) {
        const int value =
            scroll->minimum() + (scroll->maximum() - scroll->minimum()) * frame / kFrames;
        scroll->setValue(value);
        QElapsedTimer timer;
        timer.start();
        window.render(&target);
        QApplication::processEvents();
        frameMillis.push_back(static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0);
    }
    std::sort(frameMillis.begin(), frameMillis.end());
    const double p95 =
        frameMillis[static_cast<std::size_t>(static_cast<double>(frameMillis.size()) * 0.95)];
    const double median = frameMillis[frameMillis.size() / 2];
    INFO("100k scroll: median=" << median << "ms p95=" << p95 << "ms (budget 16.7ms)");
    CHECK(p95 < 16.7);
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
