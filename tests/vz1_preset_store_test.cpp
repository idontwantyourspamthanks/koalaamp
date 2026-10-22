// SPDX-License-Identifier: MIT
//
// VZ-1 preset-library tests (docs/tickets/VZ-1-visualizer.md task 3): the
// recursive scan, the watcher that keeps the list current without a restart, and
// the ordered/random selection the window cycles through. Qt Core only — no GL, no
// window, no platform plugin.
#include "ui/PresetCycler.hpp"
#include "ui/PresetSoak.hpp"
#include "ui/PresetStore.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace {

using koalaamp::ui::PresetCycler;
using koalaamp::ui::PresetOrder;
using koalaamp::ui::PresetSoak;
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

/// Path equality that ignores the separator flavour. On Windows the store returns
/// native paths ("C:\\dir\\a.milk") while a path built from QTemporaryDir's
/// "/"-separated string spells itself with forward slashes; both name one file.
bool samePath(const std::string& a, const std::string& b) {
    std::error_code ec;
    const bool same = std::filesystem::equivalent(a, b, ec);
    return same && !ec;
}

bool containsPath(const std::vector<std::string>& list, const std::string& path) {
    return std::any_of(list.begin(), list.end(),
                       [&path](const std::string& entry) { return samePath(entry, path); });
}

} // namespace

TEST_CASE("VZ-1 preset files are recognised case-insensitively", "[vz1][presets]") {
    CHECK(PresetStore::isPresetFile(QStringLiteral("a.milk")));
    CHECK(PresetStore::isPresetFile(QStringLiteral("a.milk2")));
    CHECK(PresetStore::isPresetFile(QStringLiteral("A.MILK")));
    CHECK(PresetStore::isPresetFile(QStringLiteral("Some Preset.Milk2")));
    CHECK_FALSE(PresetStore::isPresetFile(QStringLiteral("a.jpg")));
    CHECK_FALSE(PresetStore::isPresetFile(QStringLiteral("notes.txt")));
    CHECK_FALSE(PresetStore::isPresetFile(QStringLiteral("milk")));
}

TEST_CASE("VZ-1 the store scans a root recursively and ignores everything else", "[vz1][presets]") {
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
    CHECK(containsPath(store.presets(), (root / "first.milk").string()));
    CHECK(containsPath(store.presets(), (root / "sub" / "second.milk2").string()));
    CHECK(containsPath(store.presets(), (root / "sub" / "THIRD.MILK").string()));
    CHECK(std::is_sorted(store.presets().begin(), store.presets().end()));
    // Absolute paths: the GL thread will hand them to projectM.
    CHECK(std::filesystem::path(store.presets().front()).is_absolute());
    // A first scan selects something: the window then has a preset to load.
    CHECK(store.current() == store.presets().front());
    CHECK(store.currentIndex() == 0);
}

TEST_CASE("VZ-1 the store's selection walks the list, wraps, and can be random", "[vz1][presets]") {
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
    CHECK(store.next()); // wraps
    CHECK(store.current() == list[0]);
    CHECK(store.previous()); // ...and wraps back
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
    INFO("after adding: size=" << store.size() << " current=" << store.current()
                               << " watching=" << store.isWatching());
    CHECK(listChanges >= 1);
    // ...and without disturbing the selection.
    CHECK(samePath(store.current(), (root / "one.milk").string()));

    // Deleting the selected preset moves the selection to one that survives.
    std::filesystem::remove(root / "one.milk");
    CHECK(waitFor([&store] { return store.size() == 1; }));
    INFO("after removing: size=" << store.size() << " current=" << store.current());
    CHECK(store.currentIndex() == 0);
    CHECK(samePath(store.current(), (root / "two.milk").string()));
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
    int orderChanges = 0;
    int lockChanges = 0;
    QObject::connect(&cycler, &PresetCycler::orderChanged,
                     [&orderChanges](PresetOrder) { ++orderChanges; });
    QObject::connect(&cycler, &PresetCycler::lockedChanged,
                     [&lockChanges](bool) { ++lockChanges; });

    CHECK(cycler.order() == PresetOrder::Ordered);
    CHECK(store.order() == PresetOrder::Ordered);
    CHECK(cycler.isCycling()); // unlocked, interval > 0, presets to move through

    CHECK(cycler.next());
    CHECK(store.currentIndex() == 1);
    CHECK(cycler.previous());
    CHECK(store.currentIndex() == 0);

    cycler.toggleOrder();
    CHECK(cycler.order() == PresetOrder::Random);
    CHECK(orderChanges == 1);
    CHECK(store.order() == PresetOrder::Random); // the store owns the order
    cycler.setOrder(PresetOrder::Ordered);
    CHECK(store.order() == PresetOrder::Ordered);
    CHECK(orderChanges == 2);

    // Locked: no auto-advance, but the keys still move.
    cycler.setLocked(true);
    CHECK_FALSE(cycler.isCycling());
    CHECK(lockChanges == 1);
    CHECK(cycler.next());
    CHECK(store.currentIndex() == 1);
    cycler.toggleLocked();
    CHECK_FALSE(cycler.isLocked());
    CHECK(lockChanges == 2);
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
    CHECK_FALSE(cycler.isCycling()); // nothing to cycle through
    CHECK_FALSE(cycler.next());
    cycler.setIntervalMs(20);
    CHECK_FALSE(cycler.isCycling());
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("VZ-1 the soak holds every preset once and reports its frames", "[vz1][presets]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdString());
    writeFile(root / "a.milk");
    writeFile(root / "b.milk");
    writeFile(root / "c.milk");

    PresetStore store;
    store.setRoots({root.string()});
    PresetCycler cycler(store);

    // A renderer that keeps drawing, so nothing looks stalled.
    std::uint64_t frames = 0;
    QTimer frameClock;
    frameClock.setInterval(2);
    QObject::connect(&frameClock, &QTimer::timeout, [&frames] { frames += 7; });
    frameClock.start();

    PresetSoak soak(store, cycler, [&frames] { return frames; });
    int exitCode = -1;
    QObject::connect(&soak, &PresetSoak::finished,
                     [&exitCode](int code, const QString& /*reason*/) { exitCode = code; });

    soak.start(40);
    CHECK(waitFor([&exitCode] { return exitCode >= 0; }));
    frameClock.stop();

    CHECK(exitCode == 0);
    CHECK_FALSE(soak.stalled());
    // One line per preset, each carrying its frames and fps...
    const auto fpsLines =
        std::count_if(soak.lines().begin(), soak.lines().end(), [](const std::string& line) {
            return line.find(" fps") != std::string::npos;
        });
    CHECK(fpsLines == 3);
    // ...plus the header and the summary.
    CHECK(soak.lines().size() >= 5);
    CHECK(soak.lines().front().find("3 preset(s)") != std::string::npos);
    // The soak drives the cycler itself, so the cycler must not keep a timer.
    CHECK_FALSE(cycler.isCycling());
}

TEST_CASE("VZ-1 the soak calls out a preset that stops rendering", "[vz1][presets]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdString());
    writeFile(root / "a.milk");
    writeFile(root / "b.milk");

    PresetStore store;
    store.setRoots({root.string()});
    PresetCycler cycler(store);

    // A renderer that never draws: the case the nightly soak exists to catch.
    std::uint64_t frames = 0;
    PresetSoak soak(store, cycler, [&frames] { return frames; });
    int exitCode = -1;
    QObject::connect(&soak, &PresetSoak::finished,
                     [&exitCode](int code, const QString& /*reason*/) { exitCode = code; });

    // Well under five seconds (production) and under the hold, so the stall is
    // what ends this run rather than the soak completing.
    soak.setStallMs(5);
    soak.start(200);
    CHECK(waitFor([&exitCode] { return exitCode >= 0; }, 3000));

    CHECK(exitCode == 2);
    CHECK(soak.stalled());
    REQUIRE_FALSE(soak.lines().empty());
    CHECK(soak.lines().back().find("STALL") != std::string::npos);
}
