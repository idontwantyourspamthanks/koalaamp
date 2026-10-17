// SPDX-License-Identifier: MIT
//
// VZ-1 preset-library tests (docs/tickets/VZ-1-visualizer.md task 3): the
// recursive scan, the watcher that keeps the list current without a restart, and
// the ordered/random selection the window cycles through. Qt Core only — no GL, no
// window, no platform plugin.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QThread>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/PresetCycler.hpp"
#include "ui/PresetStore.hpp"

namespace {

using koalaamp::ui::PresetCycler;
using koalaamp::ui::PresetOrder;
using koalaamp::ui::PresetStore;

void writeFile(const std::filesystem::path& path, const std::string& contents = "// preset\n") {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << contents;
}

/// Spins the event loop until `ready()` or the timeout. The watcher path is
/// asynchronous by nature: inotify -> Qt event -> debounce timer -> rescan.
bool waitFor(const std::function<bool()>& ready, int timeoutMs = 6000) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return ready();
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

}  // namespace

TEST_CASE("VZ-1 preset files are recognised case-insensitively", "[vz1][presets]") {
    CHECK(PresetStore::isPresetFile(QStringLiteral("a.milk")));
    CHECK(PresetStore::isPresetFile(QStringLiteral("a.milk2")));
    CHECK(PresetStore::isPresetFile(QStringLiteral("A.MILK")));
    CHECK(PresetStore::isPresetFile(QStringLiteral("Some Preset.Milk2")));
    CHECK_FALSE(PresetStore::isPresetFile(QStringLiteral("a.jpg")));
    CHECK_FALSE(PresetStore::isPresetFile(QStringLiteral("notes.txt")));
    CHECK_FALSE(PresetStore::isPresetFile(QStringLiteral("milk")));
}

TEST_CASE("VZ-1 the store scans a root recursively and ignores everything else",
          "[vz1][presets]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdString());

    writeFile(root / "first.milk");
    writeFile(root / "sub" / "second.milk2");
    writeFile(root / "sub" / "THIRD.MILK");
    writeFile(root / "sub" / "notes.txt");
    writeFile(root / "sub" / "cover.jpg");
    // Hidden directories are not walked (the library scanner's rule).
    writeFile(root / ".cache" / "hidden.milk");

    PresetStore store;
    store.setRoots({root.string()});

    REQUIRE(store.size() == 3);
    CHECK(contains(store.presets(), (root / "first.milk").string()));
    CHECK(contains(store.presets(), (root / "sub" / "second.milk2").string()));
    CHECK(contains(store.presets(), (root / "sub" / "THIRD.MILK").string()));
    CHECK(std::is_sorted(store.presets().begin(), store.presets().end()));
    // Absolute paths: the GL thread will hand them to projectM.
    CHECK(store.presets().front().front() == '/');
    // A first scan selects something: the window then has a preset to load.
    CHECK(store.current() == store.presets().front());
    CHECK(store.currentIndex() == 0);
}

TEST_CASE("VZ-1 the store's selection walks the list, wraps, and can be random",
          "[vz1][presets]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdString());
    writeFile(root / "a.milk");
    writeFile(root / "b.milk");
    writeFile(root / "c.milk");

    PresetStore store;
    store.setRoots({root.string()});
    REQUIRE(store.size() == 3);
    const std::vector<std::string> list = store.presets();

    CHECK(store.next());
    CHECK(store.current() == list[1]);
    CHECK(store.next());
    CHECK(store.current() == list[2]);
    CHECK(store.next());  // wraps
    CHECK(store.current() == list[0]);
    CHECK(store.previous());  // ...and wraps back
    CHECK(store.current() == list[2]);

    CHECK_FALSE(store.selectIndex(-1));
    CHECK_FALSE(store.selectIndex(3));
    CHECK(store.selectIndex(1));
    CHECK(store.current() == list[1]);
    CHECK(store.selectPath((root / "c.milk").string()));
    CHECK(store.current() == list[2]);

    // Random order never repeats the current preset while there is a choice.
    store.setOrder(PresetOrder::Random);
    store.setRandomSeed(1234);
    for (int i = 0; i < 8; ++i) {
        const std::string before = store.current();
        REQUIRE(store.next());
        CHECK(store.current() != before);
        CHECK(contains(list, store.current()));
    }
}

TEST_CASE("VZ-1 the store's watcher picks up added and removed presets", "[vz1][presets]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdString());
    writeFile(root / "one.milk");

    PresetStore store;
    store.setRoots({root.string()});
    REQUIRE(store.size() == 1);
    int listChanges = 0;
    int moved = 0;
    QObject::connect(&store, &PresetStore::listChanged, [&listChanges] { ++listChanges; });
    QObject::connect(&store, &PresetStore::currentChanged, [&moved] { ++moved; });
    store.setWatching(true);
    CHECK(store.isWatching());

    // A preset dropped into the folder appears without a restart...
    writeFile(root / "two.milk");
    CHECK(waitFor([&store] { return store.size() == 2; }));
    CHECK(listChanges >= 1);
    // ...and without disturbing the selection.
    CHECK(store.current() == (root / "one.milk").string());

    // Deleting the selected preset moves the selection to one that survives.
    std::filesystem::remove(root / "one.milk");
    CHECK(waitFor([&store] { return store.size() == 1; }));
    CHECK(store.currentIndex() == 0);
    CHECK(store.current() == (root / "two.milk").string());
    CHECK(moved >= 1);

    store.setWatching(false);
    CHECK_FALSE(store.isWatching());
}

TEST_CASE("VZ-1 a store with no presets is quiet, not broken", "[vz1][presets]") {
    PresetStore store;
    CHECK(store.empty());
    CHECK(store.current().empty());
    CHECK(store.currentIndex() == -1);
    CHECK_FALSE(store.next());
    CHECK_FALSE(store.previous());
    CHECK_FALSE(store.selectIndex(0));

    // A root that does not exist yet is kept: the watcher watches its nearest
    // existing ancestor so the folder can appear later.
    store.setRoots({"/nonexistent/koalaamp/presets"});
    CHECK(store.empty());
    store.setWatching(true);
    CHECK(store.isWatching());
    CHECK_FALSE(store.next());
}

TEST_CASE("VZ-1 the cycler advances the store, and locking stops the timer", "[vz1][presets]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdString());
    writeFile(root / "a.milk");
    writeFile(root / "b.milk");
    writeFile(root / "c.milk");

    PresetStore store;
    store.setRoots({root.string()});
    PresetCycler cycler(store);

    CHECK(cycler.order() == PresetOrder::Ordered);
    CHECK(store.order() == PresetOrder::Ordered);
    CHECK(cycler.isCycling());  // unlocked, interval > 0, presets to move through

    CHECK(cycler.next());
    CHECK(store.currentIndex() == 1);
    CHECK(cycler.previous());
    CHECK(store.currentIndex() == 0);

    cycler.toggleOrder();
    CHECK(cycler.order() == PresetOrder::Random);
    CHECK(store.order() == PresetOrder::Random);  // the store owns the order
    cycler.setOrder(PresetOrder::Ordered);
    CHECK(store.order() == PresetOrder::Ordered);

    // Locked: no auto-advance, but the keys still move.
    cycler.setLocked(true);
    CHECK_FALSE(cycler.isCycling());
    CHECK(cycler.next());
    CHECK(store.currentIndex() == 1);
    cycler.toggleLocked();
    CHECK_FALSE(cycler.isLocked());
    CHECK(cycler.isCycling());

    // A non-positive interval is another way to stop the timer (settings may use it).
    cycler.setIntervalMs(0);
    CHECK_FALSE(cycler.isCycling());
    cycler.setIntervalMs(20);
    CHECK(cycler.isCycling());

    // ...and the timer really does advance the selection.
    const std::string before = store.current();
    CHECK(waitFor([&store, &before] { return store.current() != before; }));
    CHECK(cycler.next());
    CHECK(cycler.previous());
}

TEST_CASE("VZ-1 an empty store leaves the cycler idle", "[vz1][presets]") {
    PresetStore store;
    PresetCycler cycler(store);
    CHECK_FALSE(cycler.isCycling());  // nothing to cycle through
    CHECK_FALSE(cycler.next());
    cycler.setIntervalMs(20);
    CHECK_FALSE(cycler.isCycling());
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
