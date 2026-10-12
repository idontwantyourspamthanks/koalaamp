// SPDX-License-Identifier: MIT
//
// SE-4 Part B — the snap/dock manager, keep-on-top and layout persistence.
//
// Scripted offscreen drags drive the real widgets; the manager reacts to
// `SkinnedWindow::dragFinished` and to live `QEvent::Move` events exactly as in
// the app. Cases:
//   * snap within the 10 px tolerance and dock;
//   * a gap outside the tolerance does not snap;
//   * the dock anchor drags the whole group (live, relative offsets preserved);
//   * dragging an attached window detaches it (the rest stay put);
//   * keep-on-top (`config_aot`) flips every window's flag;
//   * layout save/restore round-trips exactly through SkinDb, including
//     visibility, and older records stay visible.
#include <filesystem>
#include <sstream>
#include <string>

#include <QApplication>
#include <QPoint>
#include <QTest>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "skins/SkinDb.hpp"
#include "ui/EqWindow.hpp"
#include "ui/MainWindow.hpp"
#include "ui/PleditState.hpp"
#include "ui/PlaylistWindow.hpp"
#include "ui/WindowSet.hpp"

namespace {

namespace ui = koalaamp::ui;
using WindowId = ui::WindowSet::WindowId;

/// A fresh, shown set with no skin and no dialogs.
struct Scene {
    ui::MainWindow main;
    ui::EqWindow eq;
    ui::PlaylistWindow playlist;
    ui::WindowSet set{main, eq, playlist};

    Scene() {
        eq.setPresetDialogsEnabled(false);
        playlist.setJumpDialogEnabled(false);
        set.attach();
        set.showAll();
        QApplication::processEvents();
    }
};

/// Scripted title-bar drag of `w` so its top-left ends at `target`.
/// The drag offset captured at press is the press-local point, so each move's
/// local position is `pressLocal + (goal - current)`.
void dragTo(QWidget* w, const QPoint& target) {
    const QPoint pressLocal(120, 7);
    const QPoint start = w->pos();
    QTest::mousePress(w, Qt::LeftButton, Qt::KeyboardModifiers(), pressLocal);
    QApplication::processEvents();
    for (int step = 1; step <= 2; ++step) {
        const QPoint goal = start + (target - start) * step / 2;
        const QPoint local = pressLocal + (goal - w->pos());
        QTest::mouseMove(w, local);
        QApplication::processEvents();
    }
    const QPoint releaseLocal = pressLocal + (target - w->pos());
    QTest::mouseRelease(w, Qt::LeftButton, Qt::KeyboardModifiers(), releaseLocal);
    QApplication::processEvents();
}

/// Hide the playlist through the set so it drops out of the dock maths.
void onlyMainAndEq(Scene& scene) {
    scene.main.playlistToggled(false);
    scene.set.recomputeDockGroups();
}

}  // namespace

TEST_CASE("SE-4 drag snaps to a sibling edge within 10 px and docks", "[se4][snap]") {
    Scene scene;
    onlyMainAndEq(scene);
    scene.main.move(100, 100);
    scene.eq.move(400, 300);  // far apart: not docked
    scene.set.recomputeDockGroups();
    CHECK_FALSE(scene.set.docked(WindowId::Main, WindowId::Eq));

    // Release 5 px right / 4 px below the main window's top-right corner.
    dragTo(&scene.eq, QPoint(380, 104));
    CHECK(scene.eq.pos() == QPoint(375, 100));  // snapped flush to main's edge
    CHECK(scene.set.docked(WindowId::Main, WindowId::Eq));
}

TEST_CASE("SE-4 a gap outside the tolerance does not snap", "[se4][snap]") {
    Scene scene;
    onlyMainAndEq(scene);
    scene.main.move(100, 100);
    scene.eq.move(400, 300);
    scene.set.recomputeDockGroups();

    dragTo(&scene.eq, QPoint(400, 100));  // 25 px right of main: outside 10 px
    CHECK(scene.eq.pos() == QPoint(400, 100));
    CHECK_FALSE(scene.set.docked(WindowId::Main, WindowId::Eq));
}

TEST_CASE("SE-4 the dock anchor drag carries the group", "[se4][dock]") {
    Scene scene;
    onlyMainAndEq(scene);
    scene.main.move(100, 100);
    scene.eq.move(375, 100);  // flush: docked
    scene.set.recomputeDockGroups();
    REQUIRE(scene.set.docked(WindowId::Main, WindowId::Eq));

    dragTo(&scene.main, QPoint(150, 140));
    CHECK(scene.main.pos() == QPoint(150, 140));
    CHECK(scene.eq.pos() == QPoint(425, 140));  // moved by the same (+50, +40)
    CHECK(scene.set.docked(WindowId::Main, WindowId::Eq));
}

TEST_CASE("SE-4 dragging an attached window detaches it", "[se4][dock]") {
    Scene scene;
    onlyMainAndEq(scene);
    scene.main.move(100, 100);
    scene.eq.move(375, 100);
    scene.set.recomputeDockGroups();
    REQUIRE(scene.set.docked(WindowId::Main, WindowId::Eq));

    dragTo(&scene.eq, QPoint(500, 300));  // pull it well away
    CHECK(scene.main.pos() == QPoint(100, 100));  // the rest stayed put
    CHECK(scene.eq.pos() == QPoint(500, 300));
    CHECK_FALSE(scene.set.docked(WindowId::Main, WindowId::Eq));
}

TEST_CASE("SE-4 keep-on-top applies to every window in the set", "[se4][zorder]") {
    Scene scene;
    scene.set.setKeepOnTop(true);
    CHECK(scene.set.keepOnTop());
    CHECK(scene.main.isAlwaysOnTop());
    CHECK(scene.eq.isAlwaysOnTop());
    CHECK(scene.playlist.isAlwaysOnTop());

    scene.set.setKeepOnTop(false);
    CHECK_FALSE(scene.main.isAlwaysOnTop());
    CHECK_FALSE(scene.eq.isAlwaysOnTop());
    CHECK_FALSE(scene.playlist.isAlwaysOnTop());
}

TEST_CASE("SE-4 layout round-trips exactly through SkinDb", "[se4][layout]") {
    namespace fs = std::filesystem;
    const fs::path skin = "/tmp/koalaamp-se4-layout.wsz";

    Scene scene;
    scene.main.move(11, 22);
    scene.eq.move(286, 22);
    scene.eq.setShade(true);
    scene.playlist.move(11, 138);
    scene.playlist.applyGeometry(ui::PleditGeometry{425, 174, true});
    scene.main.playlistToggled(false);  // hide via the set (tracked visibility)
    scene.set.recomputeDockGroups();

    const koalaamp::skins::SkinLayoutState before = scene.set.layout();
    CHECK(before.main == koalaamp::skins::WindowPlacement{11, 22});
    CHECK(before.eqShade);
    CHECK(before.playlistShade);
    CHECK_FALSE(before.playlistVisible);

    koalaamp::skins::SkinDb db;
    scene.set.persistLayout(db, skin);
    const fs::path file = fs::temp_directory_path() / "koalaamp-se4-skindb.txt";
    REQUIRE(db.saveTo(file));

    koalaamp::skins::SkinDb reloaded;
    REQUIRE(reloaded.loadFrom(file));
    const koalaamp::skins::SkinLayoutState* restored = reloaded.layout(skin);
    REQUIRE(restored != nullptr);
    CHECK(*restored == before);  // byte-exact: positions, shades, visibility

    // Apply it to a fresh set and re-snapshot: identical.
    Scene other;
    other.set.applyLayout(*restored);
    CHECK(other.main.pos() == QPoint(11, 22));
    CHECK(other.eq.pos() == QPoint(286, 22));
    CHECK(other.eq.isShaded());
    CHECK(other.playlist.isShaded());
    CHECK_FALSE(other.playlist.isVisible());
    CHECK(other.set.layout() == before);

    std::error_code ec;
    fs::remove(file, ec);
}

TEST_CASE("SE-4 older layout records restore windows visible", "[se4][layout]") {
    // Pre-SE-4 records carry flags <= 7 (no visibility bits): windows show.
    std::istringstream in("layout\t1\t2\t3\t4\t5\t6\t275\t116\t5\t/skin.wsz\n");
    koalaamp::skins::SkinDb db;
    db.load(in);
    const auto* state = db.layout("/skin.wsz");
    REQUIRE(state != nullptr);
    CHECK(state->playlistShade);  // flag bit 4 set
    CHECK(state->mainVisible);
    CHECK(state->eqVisible);
    CHECK(state->playlistVisible);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
