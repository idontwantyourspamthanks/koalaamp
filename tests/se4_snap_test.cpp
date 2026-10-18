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
#include <QGuiApplication>
#include <QPoint>
#include <QScreen>
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

TEST_CASE("SE-6 the set opens in the classic arrangement", "[se6][arrange]") {
    // First run (no saved layout): the three windows used to land wherever the
    // compositor put them - the same spot for all of them, which reads as one
    // ambiguous pile and hides the docking SE-4 implements.
    Scene scene;
    const QRect arranged = scene.set.applyDefaultArrangement();

    const QRect mainRect(scene.main.pos(), scene.main.size());
    const QRect eqRect(scene.eq.pos(), scene.eq.size());
    const QRect listRect(scene.playlist.pos(), scene.playlist.size());

    // Winamp's shape: the EQ flush against the player's right edge, the playlist
    // flush beneath it.
    CHECK(eqRect.topLeft() == QPoint(mainRect.right() + 1, mainRect.top()));
    CHECK(listRect.topLeft() == QPoint(mainRect.left(), mainRect.bottom() + 1));

    const QScreen* screen = QGuiApplication::primaryScreen();
    REQUIRE(screen != nullptr);
    const QRect available = screen->availableGeometry();
    const bool fits =
        available.width() >= arranged.width() && available.height() >= arranged.height();
    if (fits) {
        CHECK(available.contains(mainRect));
        CHECK(available.contains(eqRect));
        CHECK(available.contains(listRect));
        CHECK(available.contains(arranged));
    } else {
        // Larger than the screen: pinned to its origin rather than hung off it.
        CHECK(mainRect.topLeft() == available.topLeft());
    }

    // Flush-adjacent, so the set is docked from the start: the first drag of the
    // player carries the whole group, which is what makes the set feel like one
    // window rather than three.
    CHECK(scene.set.dockGroup(WindowId::Main).size() == 3);
}

TEST_CASE("SE-4 the visualizer's slot snaps to the set and round-trips through the layout",
          "[se4]") {
    // The set holds the visualizer type-erased, so a bare SkinnedWindow stands in
    // for the GL window that only exists in a build with OpenGLWidgets.
    class StubVisualizer : public ui::SkinnedWindow {
    public:
        bool isTitleBarAt(const QPoint&) const override { return false; }
    };

    Scene scene;
    StubVisualizer visualizer;
    scene.set.setVisualizerWindow(&visualizer);
    CHECK(scene.set.visualizerWindow() == &visualizer);

    // Snapping pulls it flush to the player's right edge (3 px is inside Winamp's
    // 10 px tolerance) without joining the dock groups.
    const auto dockGroupsBefore = scene.set.dockGroups();
    visualizer.resize(200, 120);
    visualizer.move(scene.main.x() + scene.main.width() + 3, scene.main.y() + 40);
    scene.set.snapVisualizer();
    CHECK(visualizer.x() == scene.main.x() + scene.main.width());
    // Snapping must not enrol it: the dock groups are exactly what they were.
    CHECK(scene.set.dockGroups() == dockGroupsBefore);

    // A registered window is recorded -- its position, and nothing about whether it
    // is open, so a later launch cannot be talked into hiding it.
    const auto recorded = scene.set.layout();
    REQUIRE(recorded.visualizer.has_value());
    CHECK(recorded.visualizer->x == visualizer.x());
    CHECK(recorded.visualizer->y == visualizer.y());

    // ...and the record restores the position, which is the half that reads the
    // layout2 fields.
    visualizer.move(700, 700);
    scene.set.applyLayout(recorded);
    CHECK(visualizer.x() == recorded.visualizer->x);
    CHECK(visualizer.y() == recorded.visualizer->y);

    // A layout that predates the visualizer leaves it alone.
    visualizer.move(33, 44);
    scene.set.applyLayout(koalaamp::skins::SkinLayoutState{});
    CHECK(visualizer.x() == 33);
    CHECK(visualizer.y() == 44);
}

TEST_CASE("SE-4 the set scales every window, the visualizer slot included", "[se4]") {
    // The slot is held type-erased, so the base's virtual is how it hears about a
    // scale change -- without it a 2x choice would leave the visualizer at 1x.
    class ScalingVisualizer : public ui::SkinnedWindow {
    public:
        bool isTitleBarAt(const QPoint&) const override { return false; }
        void setScale(int scale) override { given = scale; }
        int given = 0;
    };

    Scene scene;
    ScalingVisualizer visualizer;
    scene.set.setVisualizerWindow(&visualizer);

    scene.set.setScale(3);
    CHECK(scene.main.scale() == 3);
    CHECK(scene.eq.scale() == 3);
    CHECK(scene.playlist.scale() == 3);
    CHECK(visualizer.given == 3);
    CHECK(scene.main.size().width() == ui::MainWindow::kWidth * 3);

    scene.set.setScale(1);
    CHECK(scene.main.scale() == 1);
    CHECK(visualizer.given == 1);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
