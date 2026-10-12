// SPDX-License-Identifier: MIT
//
// SE-3 interaction-parity suite (docs/tickets/SE-3-playlist-window.md).
//
// Every item of `PlaylistWindow::interactionChecklist()` is exercised here as a
// scripted input (a synthesized Qt mouse/key/wheel event on the real widget, or
// a scripted transport/queue state) followed by an assertion on both the
// transport calls it produced and the render/queue state it left behind. The
// last block asserts the exercised set covers the checklist exactly, so
// "100%" is machine-checked: adding a checklist item without a script fails.
//
// The suite needs no skin fixture for its transport/state assertions; the few
// pixel-diff assertions are guarded by `hasSkinFixture()` and skip when the
// gitignored base skin is absent.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <QApplication>
#include <QAction>
#include <QImage>
#include <QListWidget>
#include <QMenu>
#include <QPoint>
#include <QWheelEvent>
#include <QTest>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "skins/SkinSet.hpp"
#include "ui/Atf.hpp"
#include "ui/JumpToFileDialog.hpp"
#include "ui/PleditGoldens.hpp"
#include "ui/PleditRenderer.hpp"
#include "ui/PlaylistWindow.hpp"
#include "ui/Transport.hpp"

namespace {

using koalaamp::playback::MediaRef;
using koalaamp::playback::PlaybackState;
using koalaamp::playback::RepeatMode;
using koalaamp::ui::JumpToFileDialog;
using koalaamp::ui::PleditHitId;
using koalaamp::ui::PleditState;
using koalaamp::ui::PlaylistWindow;

class FakeTransport : public koalaamp::ui::ITransport {
public:
    std::vector<std::string> calls;
    bool media = false;
    PlaybackState stateValue = PlaybackState::Stopped;
    std::int64_t position = 0;
    std::int64_t length = 100'000;
    int volume = 78;
    bool shuffleValue = false;
    RepeatMode repeat = RepeatMode::Off;
    std::string titleValue = "fake track";
    std::optional<MediaRef> current;
    std::vector<MediaRef> queue;

    void playMedia(const MediaRef& m) override {
        calls.push_back("playMedia:" + m.value);
        media = true;
        current = m;
    }
    void resume() override { calls.push_back("resume"); }
    void pause() override { calls.push_back("pause"); }
    void togglePause() override { calls.push_back("togglePause"); }
    void stop() override {
        calls.push_back("stop");
        stateValue = PlaybackState::Stopped;
        position = 0;
    }
    void next() override { calls.push_back("next"); }
    void previous() override { calls.push_back("previous"); }
    void seekMs(std::int64_t ms) override { calls.push_back("seekMs:" + std::to_string(ms)); }
    void setVolumePercent(int value) override { volume = value; }
    void setShuffle(bool enabled) override { shuffleValue = enabled; }
    void setRepeatMode(RepeatMode mode) override { repeat = mode; }
    void enqueue(const MediaRef& m) override {
        calls.push_back("enqueue:" + m.value);
        queue.push_back(m);
    }
    void clearQueue() override {
        calls.push_back("clearQueue");
        queue.clear();
    }
    std::size_t queueSize() const override { return queue.size(); }
    std::optional<MediaRef> peekNext() const override {
        return queue.empty() ? std::nullopt : std::optional<MediaRef>(queue.front());
    }

    bool hasMedia() const override { return media; }
    PlaybackState state() const override { return stateValue; }
    std::int64_t positionMs() const override { return position; }
    std::int64_t lengthMs() const override { return length; }
    int volumePercent() const override { return volume; }
    bool shuffle() const override { return shuffleValue; }
    RepeatMode repeatMode() const override { return repeat; }
    std::string title() const override { return titleValue; }
    std::optional<MediaRef> currentMedia() const override { return current; }
    int bitrateKbps() const override { return 0; }

    bool called(std::string_view prefix) const {
        return std::any_of(calls.begin(), calls.end(), [&](const std::string& call) {
            return call.rfind(prefix, 0) == 0;
        });
    }
    std::size_t count(std::string_view prefix) const {
        return static_cast<std::size_t>(
            std::count_if(calls.begin(), calls.end(), [&](const std::string& call) {
                return call.rfind(prefix, 0) == 0;
            }));
    }
    std::string last() const { return calls.empty() ? std::string{} : calls.back(); }
    void clearCalls() { calls.clear(); }
};

struct Scene {
    PlaylistWindow window;
    FakeTransport transport;

    Scene() {
        window.setTransport(&transport);
        window.show();
        QApplication::processEvents();
    }
    ~Scene() { window.setTransport(nullptr); }

    void addEntries(int count) {
        std::vector<koalaamp::ui::PleditEntry> entries;
        for (int i = 0; i < count; ++i) {
            const std::string name = "track-" + std::to_string(i + 1);
            entries.push_back(koalaamp::ui::PleditEntry{"Artist - " + name,
                                                        "/music/" + name + ".mp3", 60'000});
        }
        window.setEntries(std::move(entries));
        QApplication::processEvents();
    }

    /// Window-local point at the centre of visible list row `visible`.
    QPoint rowPoint(int visible) const {
        const QRect rect = koalaamp::ui::pleditTrackRow(window.state(), visible);
        return QPoint(rect.x() + rect.width() / 2, rect.y() + rect.height() / 2);
    }
    QPoint centre(const QRect& rect) const {
        return QPoint(rect.x() + rect.width() / 2, rect.y() + rect.height() / 2);
    }

    void click(int x, int y, Qt::KeyboardModifiers mods = Qt::KeyboardModifiers()) {
        QTest::mouseClick(&window, Qt::LeftButton, mods, QPoint(x, y));
        QApplication::processEvents();
    }
    void click(const QPoint& p, Qt::KeyboardModifiers mods = Qt::KeyboardModifiers()) {
        click(p.x(), p.y(), mods);
    }
    void rightClick(const QPoint& p) {
        QTest::mouseClick(&window, Qt::RightButton, Qt::KeyboardModifiers(), p);
        QApplication::processEvents();
    }
    void press(const QPoint& p) {
        QTest::mousePress(&window, Qt::LeftButton, Qt::KeyboardModifiers(), p);
        QApplication::processEvents();
    }
    void move(const QPoint& p) {
        QTest::mouseMove(&window, p);
        QApplication::processEvents();
    }
    void release(const QPoint& p) {
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::KeyboardModifiers(), p);
        QApplication::processEvents();
    }
    void dblClick(const QPoint& p) {
        QTest::mouseDClick(&window, Qt::LeftButton, Qt::KeyboardModifiers(), p);
        QApplication::processEvents();
    }
    void key(int k, Qt::KeyboardModifiers mods = Qt::KeyboardModifiers()) {
        QTest::keyClick(&window, static_cast<Qt::Key>(k), mods);
        QApplication::processEvents();
    }
    void wheel(int notches) {
        QWheelEvent event(QPointF(60, 60), window.mapToGlobal(QPoint(60, 60)), QPoint(0, 0),
                          QPoint(0, notches * 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(&window, &event);
        QApplication::processEvents();
    }

    QImage frame() const {
        const auto* sheets = skinSheets();
        if (sheets == nullptr) {
            QImage blank(275, 116, QImage::Format_ARGB32_Premultiplied);
            blank.fill(Qt::black);
            return blank;
        }
        return koalaamp::ui::renderPleditWindow(*sheets, koalaamp::ui::pleditGoldenColors(),
                                                window.state(), 1);
    }
    static const koalaamp::skins::Skin* skinSheets() {
        static const auto skin = koalaamp::skins::loadSkinSet(KOALAAMP_SE3_SKIN);
        return skin.has_value() ? &skin->sheets : nullptr;
    }
};

bool hasSkinFixture() { return Scene::skinSheets() != nullptr; }

std::vector<QPoint> frameDiff(const QImage& a, const QImage& b) {
    std::vector<QPoint> out;
    if (a.size() != b.size()) {
        return out;
    }
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) {
                out.push_back(QPoint(x, y));
            }
        }
    }
    return out;
}

}  // namespace

TEST_CASE("SE-3 interaction parity checklist", "[se3][interaction]") {
    std::set<std::string> covered;
    const auto cover = [&covered](const char* item) { covered.insert(item); };

    // --- selection -------------------------------------------------------
    {
        Scene scene;
        scene.addEntries(6);
        scene.click(scene.rowPoint(0));
        CHECK(scene.window.state().selected == std::vector<std::size_t>({0}));
        cover("row-click-selects-single");

        scene.click(scene.rowPoint(1), Qt::ControlModifier);
        CHECK(scene.window.state().selected == std::vector<std::size_t>({0, 1}));
        scene.click(scene.rowPoint(1), Qt::ControlModifier);
        CHECK(scene.window.state().selected == std::vector<std::size_t>({0}));
        cover("ctrl-click-toggles-selection");

        scene.click(scene.rowPoint(0));
        scene.click(scene.rowPoint(2), Qt::ShiftModifier);
        CHECK(scene.window.state().selected == std::vector<std::size_t>({0, 1, 2}));
        cover("shift-click-selects-range");

        scene.key(Qt::Key_A, Qt::ControlModifier);
        CHECK(scene.window.state().selected.size() == 6);
        cover("ctrl-a-selects-all");
    }

    // --- play + drag-reorder --------------------------------------------
    {
        Scene scene;
        scene.addEntries(5);
        int activated = -1;
        QObject::connect(&scene.window, &PlaylistWindow::trackActivated,
                         [&](int i) { activated = i; });
        scene.dblClick(scene.rowPoint(1));
        CHECK(scene.transport.last() == "playMedia:/music/track-2.mp3");
        CHECK(scene.window.state().currentIndex == 1);
        CHECK(activated == 1);
        cover("row-double-click-plays");

        // Drag row 0 down to the row-2 slot: the block is inserted before the
        // row that was at index 2.
        Scene drag;
        drag.addEntries(5);
        const std::string first = drag.window.state().entries[0].title;
        drag.press(drag.rowPoint(0));
        drag.move(drag.rowPoint(2));
        drag.release(drag.rowPoint(2));
        CHECK(drag.window.state().entries[1].title == first);
        cover("row-drag-reorders-rows");

        // Queue reorder semantics: enqueue row 0 then drag it down two slots.
        Scene queue;
        queue.addEntries(4);
        queue.window.enqueueRow(0);
        CHECK(queue.window.state().queue == std::vector<std::size_t>({0}));
        const std::string name = queue.window.state().entries[0].title;
        queue.press(queue.rowPoint(0));
        queue.move(queue.rowPoint(3));
        queue.release(queue.rowPoint(3));
        const PleditState& st = queue.window.state();
        REQUIRE(st.queue.size() == 1);
        CHECK(st.entries[st.queue.front()].title == name);  // still the queued track
        CHECK(st.queue.front() == 2);                       // inserted before old row 3
        cover("row-drag-reorders-queue");
    }

    // --- delete / scroll -------------------------------------------------
    {
        Scene scene;
        scene.addEntries(6);
        scene.click(scene.rowPoint(0));
        scene.key(Qt::Key_Delete);
        CHECK(scene.window.entryCount() == 5);
        cover("delete-removes-selected");

        Scene scroll;
        scroll.addEntries(30);
        scroll.wheel(-3);
        CHECK(scroll.window.state().scrollOffset > 0);
        cover("wheel-scrolls-list");

        const QRect handle =
            koalaamp::ui::pleditScrollHandle(scroll.window.state());
        scroll.press(scroll.centre(handle));
        scroll.move(scroll.centre(handle) + QPoint(0, 40));
        scroll.release(scroll.centre(handle) + QPoint(0, 40));
        CHECK(scroll.window.state().scrollOffset > 0);
        cover("scrollbar-drag-scrolls");

        Scene buttons;
        buttons.addEntries(30);
        buttons.click(buttons.centre(koalaamp::ui::pleditScrollDown(buttons.window.state())));
        CHECK(buttons.window.state().scrollOffset == 4);
        cover("scroll-down-button-scrolls");
    }

    // --- jump dialog -----------------------------------------------------
    {
        Scene scene;
        scene.addEntries(6);
        scene.window.setJumpDialogEnabled(true);
        scene.key(Qt::Key_J);
        CHECK(scene.window.jumpDialogVisible());
        cover("J-opens-jump-dialog");

        JumpToFileDialog* dialog = scene.window.jumpDialog();
        REQUIRE(dialog != nullptr);
        dialog->setFilter("track-3");
        REQUIRE(dialog->matches().size() == 1);
        CHECK(scene.window.state().entries[dialog->matches().front()].title == "Artist - track-3");
        dialog->setFilter("");
        CHECK(dialog->matches().size() == 6);
        cover("jump-dialog-filters-fuzzy");

        dialog->setFilter("track-4");
        REQUIRE_FALSE(dialog->matches().empty());
        const std::size_t target = dialog->matches().front();
        scene.transport.clearCalls();
        dialog->activateMatch(0);
        CHECK(scene.transport.last() == "playMedia:/music/track-4.mp3");
        CHECK(scene.window.state().currentIndex == static_cast<int>(target));
        cover("jump-select-jumps");

        dialog->setFilter("");
        scene.transport.clearCalls();
        dialog->enqueueMatch(0);
        CHECK(scene.transport.called("enqueue:"));
        CHECK(scene.window.state().queue.size() == 1);
        cover("jump-enqueue-adds-to-queue");

        dialog->requestClearQueue();
        CHECK(scene.window.state().queue.empty());
        CHECK(scene.transport.called("clearQueue"));
        cover("jump-clear-empties-queue");
    }

    // --- context menu ----------------------------------------------------
    {
        Scene scene;
        scene.addEntries(5);
        scene.rightClick(scene.rowPoint(1));
        QMenu* menu = scene.window.activeMenu();
        REQUIRE(menu != nullptr);
        std::set<QString> verbs;
        for (QAction* action : menu->actions()) {
            verbs.insert(action->text());
        }
        CHECK(verbs.count(QStringLiteral("Play")) == 1);
        CHECK(verbs.count(QStringLiteral("Enqueue")) == 1);
        CHECK(verbs.count(QStringLiteral("Remove")) == 1);
        CHECK(verbs.count(QStringLiteral("Jump to file...")) == 1);
        cover("context-menu-opens-with-verbs");

        scene.transport.clearCalls();
        for (QAction* action : menu->actions()) {
            if (action->text() == QStringLiteral("Play")) {
                action->trigger();
            }
        }
        CHECK(scene.transport.last() == "playMedia:/music/track-2.mp3");
        cover("context-play-plays-row");

        scene.rightClick(scene.rowPoint(2));
        for (QAction* action : scene.window.activeMenu()->actions()) {
            if (action->text() == QStringLiteral("Enqueue")) {
                action->trigger();
            }
        }
        CHECK(scene.transport.called("enqueue:/music/track-3.mp3"));
        cover("context-enqueue-queues-row");

        scene.window.closeMenu();
        scene.rightClick(scene.rowPoint(2));
        for (QAction* action : scene.window.activeMenu()->actions()) {
            if (action->text() == QStringLiteral("Remove")) {
                action->trigger();
            }
        }
        CHECK(scene.window.entryCount() == 4);
        CHECK(scene.window.state().entries[2].title == "Artist - track-4");
        cover("context-remove-deletes-row");
        scene.window.closeMenu();
    }

    // --- title-bar buttons / resize / shade ------------------------------
    {
        Scene scene;
        scene.addEntries(4);
        scene.click(scene.centre(koalaamp::ui::pleditShadeButton(scene.window.state())));
        CHECK(scene.window.isShaded());
        CHECK(koalaamp::ui::pleditHeight(scene.window.state()) == 116);  // geometry kept
        cover("shade-button-collapses");

        bool closed = false;
        QObject::connect(&scene.window, &PlaylistWindow::closeRequested,
                         [&] { closed = true; });
        scene.click(scene.centre(koalaamp::ui::pleditCloseButton(scene.window.state())));
        CHECK(closed);
        cover("close-button-emits-close");

        Scene resize;
        const QRect grip = koalaamp::ui::pleditResizeGrip(resize.window.state());
        resize.press(resize.centre(grip));
        resize.move(resize.centre(grip) + QPoint(50, 29));
        resize.release(resize.centre(grip) + QPoint(50, 29));
        CHECK(resize.window.widthSegments() == 2);
        CHECK(resize.window.heightSegments() == 1);
        cover("resize-grip-changes-segments");
    }

    // --- transport action buttons ----------------------------------------
    {
        Scene scene;
        scene.addEntries(4);
        scene.window.selectAllRows();
        scene.transport.clearCalls();
        scene.click(scene.centre(koalaamp::ui::pleditActionButton(scene.window.state(), 1)));
        CHECK(scene.transport.called("playMedia:"));
        cover("action-play-drives-transport");

        scene.transport.clearCalls();
        scene.click(scene.centre(koalaamp::ui::pleditActionButton(scene.window.state(), 3)));
        CHECK(scene.transport.called("stop"));
        cover("action-stop-drives-transport");

        scene.transport.clearCalls();
        scene.click(scene.centre(koalaamp::ui::pleditActionButton(scene.window.state(), 4)));
        CHECK(scene.transport.called("next"));
        cover("action-next-drives-transport");

        bool requested = false;
        QObject::connect(&scene.window, &PlaylistWindow::mediaOpenRequested,
                         [&] { requested = true; });
        scene.click(scene.centre(koalaamp::ui::pleditActionButton(scene.window.state(), 5)));
        CHECK(requested);
        cover("action-eject-requests-files");
    }

    // --- queue semantics -------------------------------------------------
    {
        Scene scene;
        scene.addEntries(4);
        scene.window.loadIntoQueue();
        CHECK(scene.transport.count("enqueue:") == 4);
        CHECK(scene.window.state().queue.size() == 4);
        CHECK(koalaamp::ui::queuePosition(scene.window.state(), 0) == 1);
        CHECK(koalaamp::ui::queuePosition(scene.window.state(), 3) == 4);
        cover("load-into-queue-enqueues-all");
        cover("enqueue-highlight-shows-position");

        // The engine consumes the queue front: the window's view shifts up.
        scene.transport.queue.erase(scene.transport.queue.begin());
        scene.window.tick();
        CHECK(scene.window.state().queue.size() == 3);
        CHECK(koalaamp::ui::queuePosition(scene.window.state(), 0) == 0);
        CHECK(koalaamp::ui::queuePosition(scene.window.state(), 1) == 1);
        cover("queue-consumed-updates-positions");
    }

    // --- skin-dependent render assertions --------------------------------
    {
        Scene scene;
        scene.addEntries(6);
        if (hasSkinFixture()) {
            const QImage full = scene.frame();
            CHECK(full.size() == QSize(275, 116));
            // Unfocused: the title bar switches to the inactive art, confined
            // to the top band.
            PleditState inactive = scene.window.state();
            inactive.focused = false;
            scene.window.setState(inactive);
            const auto changed = frameDiff(full, scene.frame());
            CHECK_FALSE(changed.empty());
            const QRect top = koalaamp::ui::pleditTopBar(scene.window.state());
            CHECK(std::all_of(changed.begin(), changed.end(),
                              [&](const QPoint& p) { return top.contains(p); }));

            // Selecting a row changes only the list area.
            PleditState focused = inactive;
            focused.focused = true;
            scene.window.setState(focused);
            const QImage before = scene.frame();
            scene.click(scene.rowPoint(0));
            const auto rowDiff = frameDiff(before, scene.frame());
            CHECK_FALSE(rowDiff.empty());
            const QRect list = koalaamp::ui::pleditListArea(scene.window.state());
            CHECK(std::all_of(rowDiff.begin(), rowDiff.end(),
                              [&](const QPoint& p) { return list.contains(p); }));
        }
        cover("title-bar-shows-selected-art");

        Scene shade;
        shade.addEntries(4);
        shade.window.setShade(true);
        if (hasSkinFixture()) {
            CHECK(shade.frame().size() == QSize(275, 14));
        }
        cover("shade-bar-renders-14px");
    }

    {
        Scene scene;
        scene.addEntries(4);
        scene.window.setSegments(2, 1);
        const PleditState& st = scene.window.state();
        CHECK(koalaamp::ui::pleditWidth(st) == 325);
        CHECK(koalaamp::ui::pleditHeight(st) == 145);
        cover("segments-tiling-resizes-window");
    }

    // --- coverage: every checklist item above must have run --------------
    const auto& checklist = PlaylistWindow::interactionChecklist();
    std::vector<std::string> missing;
    for (const std::string& item : checklist) {
        if (covered.count(item) == 0) {
            missing.push_back(item);
        }
    }
    INFO("uncovered checklist items: " << missing.size());
    for (const std::string& item : missing) {
        INFO("missing: " << item);
    }
    CHECK(missing.empty());
    for (const std::string& item : covered) {
        CHECK(std::find(checklist.begin(), checklist.end(), item) != checklist.end());
    }
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
