// SPDX-License-Identifier: MIT
//
// PL-1 window wiring suite: the SE-3 playlist window's open/save verbs, the
// drag-drop of playlist/media files, the extension list and the autosave
// debounce/restore path. Drives the real widget offscreen (Qt Widgets + QtTest),
// like the SE-3 interaction suite.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPoint>
#include <QPointF>
#include <QTest>
#include <QUrl>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "playlist/Playlist.hpp"
#include "ui/PlaylistWindow.hpp"
#include "ui/Transport.hpp"

namespace {

using koalaamp::playback::MediaRef;
using koalaamp::playback::PlaybackState;
using koalaamp::playback::RepeatMode;
using koalaamp::ui::PlaylistWindow;

class FakeTransport : public koalaamp::ui::ITransport {
public:
    std::optional<MediaRef> current;
    std::vector<MediaRef> queue;

    void playMedia(const MediaRef& m) override { current = m; }
    void resume() override {}
    void pause() override {}
    void togglePause() override {}
    void stop() override {}
    void next() override {}
    void previous() override {}
    void seekMs(std::int64_t) override {}
    void setVolumePercent(int) override {}
    void setShuffle(bool) override {}
    void setRepeatMode(RepeatMode) override {}
    void enqueue(const MediaRef& m) override { queue.push_back(m); }
    void clearQueue() override { queue.clear(); }
    std::size_t queueSize() const override { return queue.size(); }
    std::optional<MediaRef> peekNext() const override {
        return queue.empty() ? std::nullopt : std::optional<MediaRef>(queue.front());
    }
    bool hasMedia() const override { return current.has_value(); }
    PlaybackState state() const override { return PlaybackState::Stopped; }
    std::int64_t positionMs() const override { return 0; }
    std::int64_t lengthMs() const override { return 0; }
    int volumePercent() const override { return 100; }
    bool shuffle() const override { return false; }
    RepeatMode repeatMode() const override { return RepeatMode::Off; }
    std::string title() const override { return {}; }
    std::optional<MediaRef> currentMedia() const override { return current; }
};

class ScratchDir {
public:
    explicit ScratchDir(const std::string& tag) {
        static int counter = 0;
        m_path = std::filesystem::temp_directory_path() /
                 ("koalaamp-pl1-win-" + tag + "-" + std::to_string(++counter));
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
        std::filesystem::create_directories(m_path, ec);
    }
    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }
    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

std::vector<koalaamp::playlist::Track> sampleTracks() {
    return {
        {"Alpha", "/music/alpha.mp3", 60000},
        {"Beta", "/music/beta.ogg", 30000},
        {"Gamma", "/music/gamma.flac", 0},
    };
}

}  // namespace

TEST_CASE("PlaylistWindow open/save verbs round-trip a playlist file", "[pl1][window]") {
    ScratchDir scratch("verbs");
    PlaylistWindow window;
    FakeTransport transport;
    window.setTransport(&transport);

    const auto tracks = sampleTracks();
    const std::filesystem::path source = scratch.path() / "in.m3u8";
    REQUIRE(koalaamp::playlist::writeFile(source, tracks, koalaamp::playlist::Format::M3u8));

    CHECK(window.openPlaylist(source.string()));
    REQUIRE(window.entryCount() == tracks.size());
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        CHECK(window.state().entries[i].title == tracks[i].title);
        CHECK(window.state().entries[i].filename == tracks[i].filename);
        CHECK(window.state().entries[i].lengthMs == tracks[i].lengthMs);
    }

    const std::filesystem::path out = scratch.path() / "out.pls";
    CHECK(window.savePlaylist(out.string()));
    const auto reread = koalaamp::playlist::readFile(out);
    CHECK(reread.format == koalaamp::playlist::Format::Pls);
    CHECK(reread.tracks == tracks);

    CHECK_FALSE(window.openPlaylist((scratch.path() / "missing.m3u").string()));
    CHECK_FALSE(window.savePlaylist((scratch.path() / "x.dat").string()));
    window.setTransport(nullptr);
}

TEST_CASE("PlaylistWindow add verb appends", "[pl1][window]") {
    ScratchDir scratch("add");
    PlaylistWindow window;
    const auto tracks = sampleTracks();
    const std::filesystem::path source = scratch.path() / "in.m3u";
    REQUIRE(koalaamp::playlist::writeFile(source, tracks, koalaamp::playlist::Format::M3u));

    CHECK(window.addPlaylist(source.string()));
    CHECK(window.addPlaylist(source.string()));
    CHECK(window.entryCount() == tracks.size() * 2);
}

TEST_CASE("PlaylistWindow exposes the file-association extension list", "[pl1][window]") {
    const auto& exts = PlaylistWindow::playlistExtensions();
    CHECK(std::find(exts.begin(), exts.end(), ".m3u") != exts.end());
    CHECK(std::find(exts.begin(), exts.end(), ".pls") != exts.end());
    CHECK(std::find(exts.begin(), exts.end(), ".b4s") != exts.end());
}

TEST_CASE("PlaylistWindow accepts dropped playlist and media files", "[pl1][window]") {
    ScratchDir scratch("drop");
    PlaylistWindow window;
    FakeTransport transport;
    window.setTransport(&transport);

    const auto tracks = sampleTracks();
    const std::filesystem::path playlistFile = scratch.path() / "dropped.m3u";
    REQUIRE(koalaamp::playlist::writeFile(playlistFile, tracks, koalaamp::playlist::Format::M3u));
    const std::filesystem::path mediaFile = scratch.path() / "song.mp3";
    std::ofstream(mediaFile, std::ios::binary) << "not-really-audio";

    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QString::fromStdString(playlistFile.string())),
                  QUrl::fromLocalFile(QString::fromStdString(mediaFile.string()))});

    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    CHECK(enter.isAccepted());

    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    CHECK(drop.isAccepted());
    // 3 playlist rows + 1 media row.
    CHECK(window.entryCount() == tracks.size() + 1);
    window.setTransport(nullptr);
}

TEST_CASE("PlaylistWindow autosave debounce writes and restores after a kill -9",
          "[pl1][window][autosave]") {
    ScratchDir scratch("autosave");

    // First session: mutate the list + queue, let the debounce fire.
    {
        PlaylistWindow window;
        FakeTransport transport;
        window.setTransport(&transport);
        window.setAutosaveDir(scratch.path().string());
        window.setAutosaveEnabled(true);
        window.setAutosaveDebounceMs(20);

        window.addMediaFiles({"/music/one.mp3", "/music/two.mp3", "/music/three.mp3"});
        window.enqueueRow(2);
        window.enqueueRow(0);
        QTest::qWait(150);

        CHECK(std::filesystem::exists(koalaamp::playlist::autosavePlaylistPath(scratch.path())));
        CHECK(std::filesystem::exists(koalaamp::playlist::autosaveStatePath(scratch.path())));

        // Simulate kill -9: a stale .tmp left behind must be ignored on restore.
        std::ofstream(koalaamp::playlist::autosavePlaylistPath(scratch.path()).string() + ".tmp")
            << "partial";
        window.setTransport(nullptr);
    }

    // Second session: restore.
    PlaylistWindow restored;
    restored.setAutosaveDir(scratch.path().string());
    CHECK(restored.restoreAutosave());
    REQUIRE(restored.entryCount() == 3);
    CHECK(restored.state().queue == std::vector<std::size_t>{2, 0});
}

TEST_CASE("PlaylistWindow flushes autosave on destruction", "[pl1][window][autosave]") {
    ScratchDir scratch("quit");
    {
        PlaylistWindow window;
        window.setAutosaveDir(scratch.path().string());
        window.setAutosaveEnabled(true);
        window.setAutosaveDebounceMs(60000);  // never fires during the test
        window.addMediaFiles({"/music/one.mp3"});
        CHECK_FALSE(std::filesystem::exists(koalaamp::playlist::autosavePlaylistPath(scratch.path())));
    }  // destructor flushes
    CHECK(std::filesystem::exists(koalaamp::playlist::autosavePlaylistPath(scratch.path())));
    PlaylistWindow window;
    window.setAutosaveDir(scratch.path().string());
    CHECK(window.restoreAutosave());
    CHECK(window.entryCount() == 1);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
