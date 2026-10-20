// SPDX-License-Identifier: MIT
// KoalaAmp application entry point.
//
// M0 scope was "construct the frameless toplevel"; SE-2 makes that toplevel the
// real thing: the classic main window drawn from a `.wsz` skin, wired to the
// playback engine (when the libvlc-backed core is built) through
// `ui::PlaybackCoreTransport` and its PCM tap.
//
// The window set (player + EQ + playlist) is assembled and persisted by SE-4:
// the main window's toggles drive the other two, the DSP sits between the
// engine tap and the vis feed, and per-skin layouts restore here.
//
// Usage: koalaamp [--skin <file.wsz|dir>] [--null-audio] [--always-on-top]
//                 [--self-test]
#include "ui/MainWindow.hpp"
#include "ui/PlaylistWindow.hpp"
#include "ui/ConnectionDialog.hpp"

#include "audio/AlbumEqStore.hpp"
#include "audio/DspPipeline.hpp"
#include "audio/RgTags.hpp"
#include "ui/DspEqSink.hpp"
#include "ui/EqWindow.hpp"
#include "ui/ServerBrowserWindow.hpp"
#include "ui/WindowPlatform.hpp"
#include <QCursor>
#include <QFileDialog>
#include <QMenu>

#include "ui/WindowSet.hpp"
#if defined(KOALAAMP_HAS_GL_VISUALIZER)
#include "ui/PresetCycler.hpp"
#include "ui/PresetSoak.hpp"
#include "ui/PresetStore.hpp"
#include "ui/VisualizerWindow.hpp"
#endif
#include "ui/PcmFanout.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <QApplication>
#include <QCommandLineParser>
#include <chrono>
#include <QCoreApplication>
#include <QFileDialog>
#include <QPointer>
#include <QStandardPaths>
#include <QString>
#include <QTimer>

#include "skins/SkinDb.hpp"
#include "skins/SkinSet.hpp"

#ifdef KOALAAMP_HAS_PLAYBACK_CORE
#include "playback/PlaybackCore.hpp"
#include "servers/jellyfin/JellyfinClient.hpp"
#include "servers/jellyfin/JellyfinPlayback.hpp"
#include "servers/jellyfin/JellyfinPlaybackReporter.hpp"
#include "ui/PlaybackCoreTransport.hpp"
#endif

namespace {

/// The skin DB path under the app config dir (empty when there is none).
std::string skinDbPath() {
    const QString configDir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty()) {
        return {};
    }
    return (configDir + QStringLiteral("/skindb.txt")).toStdString();
}

}  // namespace

int main(int argc, char** argv) {
    // Native Wayland cannot position or mask the window set, so request
    // XWayland (QT_QPA_PLATFORM=xcb) before Qt picks its platform plugin —
    // docs/spec.md §5.3, FE-1 findings. Offscreen/X11/macOS pass through.
    koalaamp::ui::requestXWaylandAtLaunch();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("KoalaAmp"));
    QApplication::setApplicationVersion(QStringLiteral("0.0.1"));
    QApplication::setOrganizationName(QStringLiteral("KoalaAmp"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("KoalaAmp — classic skin player"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption skinOption(QStringList{QStringLiteral("s"), QStringLiteral("skin")},
                                        QStringLiteral("Classic skin (.wsz/.zip or folder)"),
                                        QStringLiteral("path"));
    const QCommandLineOption nullAudioOption(QStringLiteral("null-audio"),
                                             QStringLiteral("Use the null audio backend"));
    const QCommandLineOption selfTestOption(QStringLiteral("self-test"),
                                            QStringLiteral("Construct, map and exit 0"));
    const QCommandLineOption alwaysOnTopOption(
        QStringLiteral("always-on-top"),
        QStringLiteral("Keep the whole window set on top (config_aot)"));
    parser.addOption(skinOption);
    parser.addOption(nullAudioOption);
    parser.addOption(selfTestOption);
    parser.addOption(alwaysOnTopOption);
#if defined(KOALAAMP_HAS_GL_VISUALIZER)
    const QCommandLineOption presetsOption(
        QStringLiteral("presets"),
        QStringLiteral("MilkDrop preset folder to scan recursively for the visualizer"),
        QStringLiteral("dir"));
    parser.addOption(presetsOption);
    const QCommandLineOption presetCycleOption(
        QStringLiteral("preset-cycle-ms"),
        QStringLiteral("Advance the visualizer to a new preset every N ms (0 = locked)"),
        QStringLiteral("ms"));
    parser.addOption(presetCycleOption);
    const QCommandLineOption soakPresetsOption(
        QStringLiteral("soak-presets"),
        QStringLiteral("Soak: hold every preset in DIR in turn, logging frames and fps"),
        QStringLiteral("dir"));
    const QCommandLineOption soakHoldOption(
        QStringLiteral("soak-hold-ms"), QStringLiteral("Soak: hold each preset for N ms"),
        QStringLiteral("ms"));
    parser.addOption(soakPresetsOption);
    parser.addOption(soakHoldOption);
#endif
    parser.process(app);

    koalaamp::ui::MainWindow window;
    koalaamp::ui::PlaylistWindow playlist;
    koalaamp::ui::EqWindow equalizer;
#if defined(KOALAAMP_HAS_GL_VISUALIZER)
    // VZ-1: the visualizer's engine is created here and handed to the window. The
    // UI layer never links the rendering back end; a build without libprojectM
    // still gets a window that says the visualizer is unavailable.
    auto visualizerEngine = koalaamp::interop::visualizer::createEngine();
    koalaamp::ui::VisualizerWindow visualizer(*visualizerEngine);
#endif
    koalaamp::ui::WindowSet windowSet(window, equalizer, playlist);

    // The skin DB carries both the last-used skin and the per-skin window
    // layout (positions, shades, visibility) SE-4 persists.
    const std::string dbPath = skinDbPath();
    koalaamp::skins::SkinDb skinDb;
    const bool hasDbPath = !dbPath.empty();
    if (hasDbPath) {
        skinDb.loadFrom(dbPath);  // missing file is not an error
    }

    std::filesystem::path skinPath = parser.value(skinOption).toStdString();
    if (skinPath.empty() && hasDbPath && !skinDb.current().empty()) {
        skinPath = skinDb.current();
    }
    std::optional<koalaamp::skins::SkinSet> skinSet;
    // SK-2: one skin-applying path. Startup and the Skins menu both come through
    // here, so "load, hand it to every window, remember it" exists once. The
    // per-skin *layout* stays the startup path's business: adopting one here would
    // race the default arrangement that runs when no layout was remembered.
    const auto applySkin = [&](const std::filesystem::path& path) {
        auto loaded = koalaamp::skins::loadSkinSet(path);
        if (!loaded.has_value()) {
            std::fprintf(stderr, "koalaamp: cannot load skin '%s'\n", path.c_str());
            return false;
        }
        skinSet = std::move(loaded);
        window.setSkin(&*skinSet);
        playlist.setSkin(&*skinSet);
        equalizer.setSkin(&*skinSet);
#if defined(KOALAAMP_HAS_GL_VISUALIZER)
        visualizer.setSkin(&*skinSet);
#endif
        skinPath = path;
        if (hasDbPath) {
            skinDb.noteLoaded(path);
        }
        for (const auto& warning : skinSet->warnings) {
            std::fprintf(stderr, "koalaamp: skin: %s\n", warning.c_str());
        }
        return true;
    };

    if (!skinPath.empty()) {
        applySkin(skinPath);
    }
    if (skinSet.has_value()) {
    } else {
        std::fprintf(stderr,
                     "koalaamp: no skin loaded — pass --skin <file.wsz>\n"
                     "          bundled skins: skins/KoalaAmp-Aqua.wsz,\n"
                     "          skins/KoalaAmp-Laser.wsz, skins/KoalaAmp-Riot.wsz,\n"
                     "          skins/KoalaAmp-Starz.wsz\n");
    }

#if defined(KOALAAMP_HAS_GL_VISUALIZER)
    if (skinSet.has_value()) {
        visualizer.setSkin(&*skinSet);
    }
    // The set owns the visualizer's slot: it snaps to its siblings and remembers
    // where and whether it was open (spec §5.3's window set). The clutterbar's V
    // and the close box ask the set, which is what records the intent.
    windowSet.setVisualizerWindow(&visualizer);
    QObject::connect(&window, &koalaamp::ui::MainWindow::visualizerToggled, &windowSet,
                     [&windowSet](bool on) { windowSet.setVisualizerVisible(on); });
    QObject::connect(&visualizer, &koalaamp::ui::VisualizerWindow::closeRequested, &windowSet,
                     [&windowSet] { windowSet.setVisualizerVisible(false); });


    // The preset library (task 3): scanning, the watcher, and the selection. The
    // cycling *policy* is task 4's — this only makes the folder's presets available,
    // loads the first one, and follows the selection when it moves.
    koalaamp::ui::PresetStore presetStore;

    // Without --presets the visualizer still needs somewhere to look, or the menu
    // has nothing to offer: the bundled starter pack comes first and the user's own
    // folder (the XDG data folder, created on demand) second, so a fresh install has
    // presets before the user adds any. --presets is authoritative. The menu can
    // point the store somewhere else, and the folder it picks is where the file
    // dialog starts next time.
    const auto defaultPresetDir = [] {
        const QByteArray xdg = qgetenv("XDG_DATA_HOME");
        const std::string home = qgetenv("HOME").toStdString();
        std::filesystem::path base =
            xdg.isEmpty() ? std::filesystem::path(home) / ".local" / "share"
                          : std::filesystem::path(xdg.toStdString());
        std::filesystem::path dir = base / "koalaamp" / "presets";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }();
    if (parser.isSet(presetsOption)) {
        presetStore.setRoots({parser.value(presetsOption).toStdString()});
    } else {
        presetStore.setRoots({KOALAAMP_BUNDLED_PRESETS, defaultPresetDir.string()});
    }
    presetStore.setWatching(true);
    if (!presetStore.empty()) {
        visualizer.canvas().requestPreset(presetStore.current());
    }
    QObject::connect(&presetStore, &koalaamp::ui::PresetStore::currentChanged, &visualizer,
                     [&visualizer](const std::string& path) {
                         if (!path.empty()) {
                             // Announce it, then load it: the load is queued into
                             // paintGL, so the name is what the user sees first.
                             visualizer.showPresetName(path);
                             visualizer.canvas().requestPreset(path);
                         }
                     });

    // Cycling (task 4): the window's MilkDrop keys ask, the cycler decides. Locked
    // stops the auto-advance; N/P keep working either way.
    koalaamp::ui::PresetCycler presetCycler(presetStore);
    if (parser.isSet(presetCycleOption)) {
        bool ok = false;
        const int interval = parser.value(presetCycleOption).toInt(&ok);
        if (ok) {
            presetCycler.setIntervalMs(interval);
            presetCycler.setLocked(interval <= 0);
        }
    }
    // VZ-1 task 8: the nightly soak. Every preset in turn, one log line each, and
    // a non-zero exit when one stops rendering (the ticket's hang).
    // Declared outside the block: the soak owns the timer that drives the whole
    // run, so letting it die at the block's closing brace would print the header
    // and then sit there as an ordinary app.
    std::unique_ptr<koalaamp::ui::PresetSoak> soak;
    if (parser.isSet(soakPresetsOption)) {
        // The soak directory is authoritative: passing --presets as well must not
        // silently soak a different corpus than the one the caller asked for.
        presetStore.setRoots({parser.value(soakPresetsOption).toStdString()});
        presetStore.setWatching(true);
        windowSet.setVisualizerVisible(true);  // nothing renders while hidden
        soak = std::make_unique<koalaamp::ui::PresetSoak>(
            presetStore, presetCycler, [&visualizer] { return visualizer.framesRendered(); });
        int holdMs = 10000;
        if (parser.isSet(soakHoldOption)) {
            bool ok = false;
            const int value = parser.value(soakHoldOption).toInt(&ok);
            if (ok && value > 0) {
                holdMs = value;
            }
        }
        QObject::connect(soak.get(), &koalaamp::ui::PresetSoak::finished, &app,
                         [&app](int code, const QString& /*reason*/) { app.exit(code); });
        soak->start(holdMs);
    }
    QObject::connect(&visualizer, &koalaamp::ui::VisualizerWindow::nextPresetRequested,
                     &presetCycler, &koalaamp::ui::PresetCycler::next);
    QObject::connect(&visualizer, &koalaamp::ui::VisualizerWindow::previousPresetRequested,
                     &presetCycler, &koalaamp::ui::PresetCycler::previous);
    QObject::connect(&visualizer, &koalaamp::ui::VisualizerWindow::orderToggleRequested,
                     &presetCycler, &koalaamp::ui::PresetCycler::toggleOrder);
    QObject::connect(&visualizer, &koalaamp::ui::VisualizerWindow::lockedToggleRequested,
                     &presetCycler, &koalaamp::ui::PresetCycler::toggleLocked);
#endif

#ifdef KOALAAMP_HAS_PLAYBACK_CORE
    koalaamp::playback::PlaybackConfig config;
    config.output = parser.isSet(nullAudioOption) ? koalaamp::playback::PlaybackConfig::Output::Null
                                                  : koalaamp::playback::PlaybackConfig::Output::Device;
    auto core = std::make_unique<koalaamp::playback::PlaybackCore>(config);

    // AU-1's DSP sits between the engine's PCM tap and the vis feed: the tap
    // installs `dsp.makeCallback(feed.callback())` at the sanctioned seam, so
    // the equalizer window's sliders reach real DSP output (docs/spec.md §3.1).
    // Declaration order matters: dsp outlives the transport that captures it.
    auto dsp = std::make_unique<koalaamp::audio::DspPipeline>();
    // Startup enable: the whole-stage flag gates ReplayGain + limiter + EQ
    // (bit-perfect-ish passthrough when false). The app wants the stage ON —
    // DspEqSink toggles only the EQ section, so it must never be the sole
    // writer of this flag (a default-false stage would leave the app's DSP
    // inert while the EQ window appears wired). RG mode/limiter config will
    // be driven from settings by AU-2's activation work.
    koalaamp::audio::DspSettings startupDsp;
    startupDsp.enabled = true;
    startupDsp.limiterEnabled = true;
    dsp->setSettings(startupDsp);
    auto eqSink = std::make_unique<koalaamp::ui::DspEqSink>(*dsp);
    auto albumEqStore = std::make_unique<koalaamp::audio::AlbumEqStore>();

    // SV-2: per-track ReplayGain activation. Server media carries the gateway's
    // `NormalizationGain` on the MediaRef; local media is read from the file's
    // tags. The hook fires before each play and at a gapless boundary, so the
    // DSP stage latches the incoming track's gain on its first block.
    auto rgTags = std::make_shared<koalaamp::audio::RgTagSource>();
    core->setTrackPrepareHook([dsp = dsp.get(), rgTags](const koalaamp::playback::MediaRef& media) {
        koalaamp::audio::RgInfo info;
        if (media.rg) {
            if (media.rg->trackGain != 0.0) {
                info.trackGainDb = media.rg->trackGain;
            }
            if (media.rg->trackPeak > 0.0) {
                info.trackPeak = media.rg->trackPeak;
            }
            if (media.rg->albumGain != 0.0) {
                info.albumGainDb = media.rg->albumGain;
            }
            if (media.rg->albumPeak > 0.0) {
                info.albumPeak = media.rg->albumPeak;
            }
        } else if (media.kind == koalaamp::playback::MediaRef::Kind::Path) {
            info = rgTags->forPath(media.value);
        }
        dsp->setReplayGain(info);
    });
    // VZ-1: the visualizer consumes the same post-DSP samples as the main window's
    // spectrum. It joins as a second reader of a fan-out after the DSP — installing
    // another engine tap would replace the DSP chain instead of adding a reader
    // (PcmFanout's rule). Declaration order matters: the fan-out outlives the
    // transport that captures its callback.
    koalaamp::ui::PcmFanout pcmFanout;
    pcmFanout.add(window.visFeed().callback());
#if defined(KOALAAMP_HAS_GL_VISUALIZER)
    pcmFanout.add(visualizer.canvas().pcmCallback());
#endif
    auto transport = std::make_unique<koalaamp::ui::PlaybackCoreTransport>(
        *core, window.visFeed(), dsp->makeCallback(pcmFanout.callback()));
    window.setTransport(transport.get());
    playlist.setTransport(transport.get());
    equalizer.setEqSink(eqSink.get());
    equalizer.setAlbumEqStore(albumEqStore.get());

    // The windows follow the engine through the transport's signals, which exist
    // for exactly this. Without these connections the UI only re-reads the
    // transport when the user presses something, and the player's tick timer —
    // the other thing that polls — starts only while something is already
    // animating: a track that begins playing asynchronously therefore never
    // showed a time or a position, and the progress bar stayed empty.
    const auto followTransport = [transport = transport.get()](auto* window) {
        const auto sync = [window](auto&&...) { window->syncFromTransport(); };
        QObject::connect(transport, &koalaamp::ui::PlaybackCoreTransport::stateChanged, window,
                         sync);
        QObject::connect(transport, &koalaamp::ui::PlaybackCoreTransport::trackChanged, window,
                         sync);
        QObject::connect(transport, &koalaamp::ui::PlaybackCoreTransport::positionChanged, window,
                         sync);
        QObject::connect(transport, &koalaamp::ui::PlaybackCoreTransport::volumeChanged, window,
                         sync);
    };
    followTransport(&window);
    followTransport(&playlist);
#else
    (void)nullAudioOption;
#endif

    // PL-1: a crash must not eat the playlist. The autosave pair (a BOM'd
    // M3U8 + the queue/index sidecar) lives under the app data dir; restore it
    // on launch and flush it on quit. The self-test smoke must stay
    // side-effect free, so it opts out.
    const QString playlistDataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!parser.isSet(selfTestOption) && !playlistDataDir.isEmpty()) {
        playlist.setAutosaveDir((playlistDataDir + QStringLiteral("/playlist")).toStdString());
        playlist.setAutosaveEnabled(true);
        playlist.restoreAutosave();
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                         [&playlist] { playlist.saveAutosaveNow(); });
    }

    // PL-1: the playlist window's open/save verbs. The parsing lives in
    // src/playlist; the app only hosts the file dialog. The filter's extension
    // list is the same one CO-1 uses for file associations.
    QStringList patterns;
    for (const std::string& ext : koalaamp::ui::PlaylistWindow::playlistExtensions()) {
        patterns << QStringLiteral("*") + QString::fromStdString(ext);
    }
    const QString filter = QStringLiteral("Playlists (%1);;All files (*)").arg(patterns.join(' '));
    QObject::connect(&playlist, &koalaamp::ui::PlaylistWindow::playlistOpenRequested, &playlist,
                     [&playlist, filter] {
                         const QString file = QFileDialog::getOpenFileName(
                             &playlist, QStringLiteral("Open playlist"), QString(), filter);
                         if (!file.isEmpty()) {
                             playlist.openPlaylist(file.toStdString());
                         }
                     });
    QObject::connect(&playlist, &koalaamp::ui::PlaylistWindow::playlistSaveRequested, &playlist,
                     [&playlist, filter] {
                         const QString file = QFileDialog::getSaveFileName(
                             &playlist, QStringLiteral("Save playlist"), QString(), filter);
                         if (!file.isEmpty()) {
                             playlist.savePlaylist(file.toStdString());
                         }
                     });

    // SE-3: the playlist's "add media" verbs. The pledit ADD menu, its transport
    // eject button and Play-with-an-empty-list all emit `mediaOpenRequested`;
    // the window hosts no dialog, and nothing else connected it, so those
    // controls were dead (drag-and-drop was the only way in). The host owns the
    // dialog here, exactly like the playlist open/save verbs above.
    //
    // Only this one signal needs the host: `MainWindow::openFiles()` (its eject
    // button / L key) already opens its own dialog and plays the chosen file,
    // and emits `ejectRequested()` merely as a notification — connecting that
    // here would open a second dialog. `MainWindow::mediaOpenRequested` fires
    // only when there is no playback core, and no dialog is opened on that path.
    const QString mediaFilter = QStringLiteral(
        "Media files (*.mp3 *.m4a *.aac *.flac *.ogg *.oga *.opus *.wav *.aiff *.ape *.wv "
        "*.wma *.mka *.mod *.xm *.it *.s3m *.umx *.mid *.cue);;All files (*)");
    const auto addMediaFiles = [&playlist, mediaFilter] {
        const QStringList files = QFileDialog::getOpenFileNames(
            &playlist, QStringLiteral("Add file(s)"), QString(), mediaFilter);
        if (files.isEmpty()) {
            return;
        }
        std::vector<std::string> paths;
        paths.reserve(static_cast<std::size_t>(files.size()));
        for (const QString& file : files) {
            paths.push_back(file.toStdString());
        }
        playlist.addMediaFiles(paths);
    };
    QObject::connect(&playlist, &koalaamp::ui::PlaylistWindow::mediaOpenRequested, &playlist,
                     addMediaFiles);
    QObject::connect(&window, &koalaamp::ui::MainWindow::mediaOpenRequested, &window,
                     addMediaFiles);

    // The window set owns the inter-window toggles: the main window's EQ/PL
    // buttons show/hide their windows; each secondary close button hides it.
    // Then restore this skin's remembered layout, or open the default set.
    // SK-2: the main window's menu. Its Option button, the clutterbar's O and a
    // right-click all ask for it. Rebuilt on open so the check marks are current.
    // Captures by reference: it runs inside this scope, while the menu is open.
#if defined(KOALAAMP_HAS_GL_VISUALIZER)
    // SV-1: the connection manager's shared state. Both live for the session — the
    // dialog borrows the transport — and the book is the identity store, never the
    // session token.
    koalaamp::servers::jellyfin::CurlTransport serverTransport;
    const QString serverBookPath =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
        QStringLiteral("/servers.json");
    koalaamp::servers::ServerBook serverBook(serverBookPath.toStdString());
    serverBook.load();

    // SV-5: the signed-in server, kept after the connection dialog closes (the
    // token stays in memory). The browser and the play path read the same copy.
    std::optional<koalaamp::servers::ServerAuth> activeServer;
#endif

#if defined(KOALAAMP_HAS_GL_VISUALIZER) && defined(KOALAAMP_HAS_PLAYBACK_CORE)
    // SV-2: report the signed-in server session's playback to Jellyfin. The
    // reporter is inert until a server media plays; the timer exists so the 10 s
    // heartbeat does not depend on position ticks (which stop while paused).
    struct ServerReporting {
        koalaamp::playback::PlaybackCore* core{nullptr};
        std::unique_ptr<koalaamp::servers::jellyfin::JellyfinClient> client;
        std::unique_ptr<koalaamp::servers::jellyfin::JellyfinPlaybackReporter> reporter;

        ServerReporting() {
            timer.setInterval(1000);
            QObject::connect(&timer, &QTimer::timeout, &timer, [this] {
                if (reporter) {
                    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch())
                                         .count();
                    reporter->tick(now);
                }
            });
        }
        ~ServerReporting() { detach(); }

        void attach(koalaamp::playback::PlaybackCore& playback,
                    koalaamp::servers::jellyfin::IHttpTransport& http,
                    const koalaamp::servers::ServerAuth& auth) {
            detach();
            koalaamp::servers::jellyfin::DeviceProfile profile;
            profile.deviceName = koalaamp::servers::jellyfin::defaultDeviceName();
            client = std::make_unique<koalaamp::servers::jellyfin::JellyfinClient>(http, profile);
            reporter = std::make_unique<koalaamp::servers::jellyfin::JellyfinPlaybackReporter>(
                *client, auth.baseUrl, auth.accessToken, auth.deviceId);
            core = &playback;
            core->addObserver(reporter.get());
            timer.start();
        }

        /// The id the planner must embed in a transcode URL for this session.
        std::string playSessionId() const {
            return reporter ? reporter->playSessionId() : std::string{};
        }

        void detach() {
            timer.stop();
            if (core != nullptr && reporter != nullptr) {
                core->removeObserver(reporter.get());
            }
            reporter.reset();
            client.reset();
            core = nullptr;
        }

        QTimer timer;
    } serverReporting;

    QPointer<koalaamp::ui::ServerBrowserWindow> serverBrowser;

    const auto openServerBrowser = [&] {
        if (!activeServer) {
            return;
        }
        if (serverBrowser) {
            serverBrowser->show();
            serverBrowser->raise();
            serverBrowser->activateWindow();
            return;
        }
        serverBrowser = new koalaamp::ui::ServerBrowserWindow(
            serverTransport, *activeServer,
            [&](const std::vector<koalaamp::library::Track>& tracks, std::size_t start,
                bool enqueueNext) {
                if (!activeServer || tracks.empty() || start >= tracks.size()) {
                    return;
                }
                koalaamp::servers::jellyfin::DeviceProfile profile;
                profile.deviceName = koalaamp::servers::jellyfin::defaultDeviceName();
                koalaamp::servers::jellyfin::JellyfinPlayback planner{serverTransport, profile};
                const std::string sessionId = serverReporting.playSessionId();
                const auto& auth = *activeServer;
                if (enqueueNext) {
                    auto plan = planner.plan(auth.baseUrl, auth.accessToken, auth.deviceId,
                                             tracks[start], sessionId);
                    core->enqueueNext(std::move(plan.media));
                    return;
                }
                for (std::size_t i = start; i < tracks.size(); ++i) {
                    auto plan = planner.plan(auth.baseUrl, auth.accessToken, auth.deviceId,
                                             tracks[i], sessionId);
                    if (i == start) {
                        core->play(plan.media);
                    } else {
                        core->enqueue(std::move(plan.media));
                    }
                }
            },
            &window);
        serverBrowser->setAttribute(Qt::WA_DeleteOnClose);
        serverBrowser->setSkin(skinSet ? &*skinSet : nullptr);
        // A stale token sends the user back to the connection manager rather
        // than leaving a browser that answers every click with 401.
        QObject::connect(serverBrowser, &koalaamp::ui::ServerBrowserWindow::sessionExpired,
                         &window, [&] {
                             if (serverBrowser) {
                                 serverBrowser->close();
                             }
                             activeServer.reset();
                             serverReporting.detach();
                         });
        serverBrowser->show();
        serverBrowser->raise();
        serverBrowser->activateWindow();
    };
#endif

    const auto showMainMenu = [&] {
        QMenu menu;

#if defined(KOALAAMP_HAS_GL_VISUALIZER)
        // One entry point for the connection manager; Browse reuses it when no
        // session is active, so a restart (tokens are memory-only) never leaves
        // the user staring at a dead menu item.
        const auto launchServersDialog = [&] {
            auto* dialog =
                new koalaamp::ui::ConnectionDialog(serverTransport, serverBook, {}, &window);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setSkin(skinSet ? &*skinSet : nullptr);
#if defined(KOALAAMP_HAS_PLAYBACK_CORE)
            // The dialog's Browse button and the main menu share one action.
            QObject::connect(dialog, &koalaamp::ui::ConnectionDialog::browseRequested, &window,
                             openServerBrowser);
            // Signing in (or out) attaches/detaches reporting for this session.
            QObject::connect(dialog, &koalaamp::ui::ConnectionDialog::sessionChanged, dialog,
                             [&, dialog] {
                                 activeServer = dialog->session();
                                 if (serverBrowser) {
                                     serverBrowser->close();
                                 }
                                 serverReporting.detach();
                                 if (activeServer) {
                                     serverReporting.attach(*core, serverTransport, *activeServer);
                                 }
                             });
#endif
            dialog->show();
            dialog->raise();
            dialog->activateWindow();
        };
        QAction* servers = menu.addAction(QStringLiteral("Servers…"));
        QObject::connect(servers, &QAction::triggered, &window, launchServersDialog);

        QAction* browse = menu.addAction(QStringLiteral("Browse server…"));
#if defined(KOALAAMP_HAS_PLAYBACK_CORE)
        QObject::connect(browse, &QAction::triggered, &window, [&] {
            if (activeServer) {
                openServerBrowser();
            } else {
                launchServersDialog(); // saved server is prefilled for re-login
            }
        });
#else
        browse->setEnabled(false);
#endif
        menu.addSeparator();
#endif
        QMenu* presets = menu.addMenu(QStringLiteral("Presets"));
        QAction* choose = presets->addAction(QStringLiteral("Choose preset folder…"));
        QObject::connect(choose, &QAction::triggered, &window, [&] {
            const auto& roots = presetStore.roots();
            const QString start =
                roots.empty() ? QString() : QString::fromStdString(roots.front());
            const QString dir = QFileDialog::getExistingDirectory(
                &window, QStringLiteral("MilkDrop preset folder"), start);
            if (!dir.isEmpty()) {
                presetStore.setRoots({dir.toStdString()});
                presetStore.setWatching(true);
            }
        });
        presets->addSeparator();
        if (presetStore.empty()) {
            QAction* none =
                presets->addAction(QStringLiteral("(empty folder — put .milk files in it)"));
            none->setEnabled(false);
        } else {
            QAction* current = presets->addAction(QString::fromStdString(
                koalaamp::ui::PresetStore::presetDisplayName(presetStore.current())));
            current->setEnabled(false);  // the name the overlay just announced
            presets->addSeparator();
            QAction* previous = presets->addAction(QStringLiteral("Previous preset"));
            QObject::connect(previous, &QAction::triggered, &window,
                             [&presetCycler] { presetCycler.previous(); });
            QAction* next = presets->addAction(QStringLiteral("Next preset"));
            QObject::connect(next, &QAction::triggered, &window,
                             [&presetCycler] { presetCycler.next(); });
            presets->addSeparator();
            QAction* random = presets->addAction(QStringLiteral("Random order"));
            random->setCheckable(true);
            random->setChecked(presetCycler.order() == koalaamp::ui::PresetOrder::Random);
            QObject::connect(random, &QAction::triggered, &window,
                             [&presetCycler] { presetCycler.toggleOrder(); });
            QAction* hold = presets->addAction(QStringLiteral("Hold this preset only"));
            hold->setCheckable(true);
            hold->setChecked(presetCycler.isLocked());
            QObject::connect(hold, &QAction::triggered, &window,
                             [&presetCycler] { presetCycler.toggleLocked(); });
        }
        menu.addSeparator();

        QMenu* skins = menu.addMenu(QStringLiteral("Skins"));
        QStringList offered;
        const auto addSkin = [&](const std::filesystem::path& path) {
            const QString name = QString::fromStdString(path.stem().string());
            if (name.isEmpty() || offered.contains(name)) {
                return;  // the bundled folder and the recent list can name one skin
            }
            offered << name;
            QAction* action = skins->addAction(name);
            action->setCheckable(true);
            action->setChecked(!skinPath.empty() && skinPath == path);
            QObject::connect(action, &QAction::triggered, &window,
                             [&applySkin, path] { applySkin(path); });
        };
        for (const auto& bundled : koalaamp::skins::listSkins(KOALAAMP_BUNDLED_SKINS)) {
            addSkin(bundled);
        }
        for (const auto& recent : skinDb.recent()) {
            addSkin(recent);
        }
        if (offered.isEmpty()) {
            QAction* none = skins->addAction(QStringLiteral("(no skins found)"));
            none->setEnabled(false);
        }
        menu.addSeparator();

        QMenu* scaling = menu.addMenu(QStringLiteral("Scaling"));
        for (int factor = 1; factor <= 4; ++factor) {
            QAction* action = scaling->addAction(QStringLiteral("%1x").arg(factor));
            action->setCheckable(true);
            action->setChecked(window.scale() == factor);
            QObject::connect(action, &QAction::triggered, &window,
                             [&windowSet, factor] { windowSet.setScale(factor); });
        }

        QAction* onTop = menu.addAction(QStringLiteral("Always on top"));
        onTop->setCheckable(true);
        onTop->setChecked(windowSet.keepOnTop());
        QObject::connect(onTop, &QAction::triggered, &windowSet,
                         [&windowSet](bool on) { windowSet.setKeepOnTop(on); });

        menu.addSeparator();
        menu.addAction(QStringLiteral("Quit"), &app, &QApplication::quit);
        menu.exec(QCursor::pos());
    };
    QObject::connect(&window, &koalaamp::ui::MainWindow::menuRequested, &window, showMainMenu);

    windowSet.attach();
    if (parser.isSet(alwaysOnTopOption)) {
        windowSet.setKeepOnTop(true);
#if defined(KOALAAMP_HAS_GL_VISUALIZER)
        visualizer.setAlwaysOnTop(true);
#endif
    }
    const bool restored = !skinPath.empty() && windowSet.restoreLayout(skinDb, skinPath);
    if (!restored) {
        // SE-6: no layout remembered for this skin — open the set in the classic
        // arrangement (EQ flush right of the player, playlist flush beneath it)
        // instead of wherever the compositor dropped the three windows, which is
        // the same spot for all of them and reads as one ambiguous pile.
        windowSet.applyDefaultArrangement();
        windowSet.showAll();
    }

    // Remember where this skin's windows were left (exact restore next launch).
    // Save while the event loop is still alive: after exec() returns, Qt has
    // already hidden the toplevels, so their visibility would read false.
    const bool persist = hasDbPath && !skinPath.empty();
    if (persist) {
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] {
            windowSet.persistLayout(skinDb, skinPath);
            skinDb.noteLoaded(skinPath);
            if (!skinDb.saveTo(dbPath)) {
                std::fprintf(stderr, "koalaamp: could not save skin DB to '%s'\n", dbPath.c_str());
            }
        });
    }

    if (parser.isSet(selfTestOption)) {
        // Headless smoke hook used by ctest (QT_QPA_PLATFORM=offscreen): prove
        // the window set constructs and maps, then exit 0.
        QTimer::singleShot(250, &app, &QCoreApplication::quit);
    }

    const int status = app.exec();
#ifdef KOALAAMP_HAS_PLAYBACK_CORE
    equalizer.setAlbumEqStore(nullptr);
    equalizer.setEqSink(nullptr);
    window.setTransport(nullptr);
    playlist.setTransport(nullptr);
#endif
    return status;
}
