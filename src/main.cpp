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
#include "audio/AlbumEqStore.hpp"
#include "audio/DspPipeline.hpp"
#include "audio/RgTags.hpp"
#include "platform/SingleInstance.hpp"
#include "servers/SecretStore.hpp"
#include "ui/ConnectionDialog.hpp"
#include "ui/DspEqSink.hpp"
#include "ui/EqWindow.hpp"
#include "ui/MainWindow.hpp"
#include "ui/MigrationDialog.hpp"
#include "ui/PlaylistWindow.hpp"
#include "ui/ServerBrowserWindow.hpp"
#include "ui/WindowPlatform.hpp"
#include "ui/WindowSet.hpp"

#include <QCryptographicHash>
#include <QCursor>
#include <QFileDialog>
#include <QMenu>
#if defined(KOALAAMP_HAS_GL_VISUALIZER)
#include "ui/PresetCycler.hpp"
#include "ui/PresetSoak.hpp"
#include "ui/PresetStore.hpp"
#include "ui/VisualizerWindow.hpp"
#endif
#include "skins/SkinDb.hpp"
#include "skins/SkinSet.hpp"
#include "ui/PcmFanout.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef KOALAAMP_HAS_PLAYBACK_CORE
#include "library/LibraryDb.hpp"
#include "library/Scanner.hpp"
#include "migration/Bookmarks.hpp"
#include "playback/PlaybackCore.hpp"
#include "playlist/Playlist.hpp"
#include "scrobble/CurlTransport.hpp"
#include "scrobble/LastFmClient.hpp"
#include "scrobble/PlaybackScrobbler.hpp"
#include "scrobble/ScrobbleService.hpp"
#include "scrobble/SecretStore.hpp"
#include "servers/jellyfin/JellyfinClient.hpp"
#include "servers/jellyfin/JellyfinPlayback.hpp"
#include "servers/jellyfin/JellyfinPlaybackReporter.hpp"
#include "servers/plex/PlexMediaSource.hpp"
#include "servers/plex/PlexTimeline.hpp"
#include "ui/JellyfinBrowserSource.hpp"
#include "ui/LibraryWindow.hpp"
#include "ui/PlaybackCoreTransport.hpp"
#include "ui/PlexBrowserSource.hpp"
#endif

namespace {

#ifdef KOALAAMP_HAS_PLAYBACK_CORE
/// Forwards the scrobble module's secret seam to the platform credential store
/// (SV-6). The slot names already carry the scrobble/ prefix, so no remapping.
class ScrobbleSecretAdapter final : public koalaamp::scrobble::ISecretStore {
public:
    explicit ScrobbleSecretAdapter(koalaamp::servers::SecretStore& store) : store_(store) {}
    bool store(std::string_view key, std::string_view secret) override {
        return store_.store(key, secret);
    }
    std::optional<std::string> load(std::string_view key) const override {
        return store_.load(key);
    }
    bool remove(std::string_view key) override { return store_.remove(key); }

private:
    koalaamp::servers::SecretStore& store_;
};
#endif

std::string environmentString(const char* name) {
    const QByteArray value = qgetenv(name);
    return value.isEmpty() ? std::string{} : value.toStdString();
}

/// Last.fm requires an application-wide API key/shared secret. These are
/// supplied at build time (or by the developer's environment); users must never
/// be asked for them.
std::string builtinLastFmApiKey() {
#ifdef KOALAAMP_BUILTIN_LASTFM_API_KEY
    return KOALAAMP_BUILTIN_LASTFM_API_KEY;
#else
    return {};
#endif
}

std::string builtinLastFmSharedSecret() {
#ifdef KOALAAMP_BUILTIN_LASTFM_SHARED_SECRET
    return KOALAAMP_BUILTIN_LASTFM_SHARED_SECRET;
#else
    return {};
#endif
}

std::string lastFmApiKey() {
    const std::string fromEnvironment = environmentString("KOALAAMP_LASTFM_API_KEY");
    return fromEnvironment.empty() ? builtinLastFmApiKey() : fromEnvironment;
}

std::string lastFmSharedSecret() {
    const std::string fromEnvironment = environmentString("KOALAAMP_LASTFM_SHARED_SECRET");
    return fromEnvironment.empty() ? builtinLastFmSharedSecret() : fromEnvironment;
}

/// The skin DB path under the app config dir (empty when there is none).
std::string skinDbPath() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty()) {
        return {};
    }
    return (configDir + QStringLiteral("/skindb.txt")).toStdString();
}

/// Stable per-user local-socket name. The config directory is already per-user;
/// hash it so the name stays short and identical across launches.
QString singleInstanceName() {
    const QByteArray key =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toUtf8();
    return QStringLiteral("koalaamp-") +
           QString::fromLatin1(
               QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex().left(16));
}

} // namespace

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
    const QCommandLineOption playOption(QStringLiteral("play"),
                                        QStringLiteral("Play the supplied files (default)"));
    const QCommandLineOption enqueueOption(
        QStringLiteral("enqueue"), QStringLiteral("Append the supplied files to the playlist"));
    QCommandLineOption noSingleInstanceOption(
        QStringLiteral("no-single-instance"),
        QStringLiteral("Do not use or start the single-instance socket (tests)"));
    noSingleInstanceOption.setFlags(QCommandLineOption::HiddenFromHelp);
    const QCommandLineOption alwaysOnTopOption(
        QStringLiteral("always-on-top"),
        QStringLiteral("Keep the whole window set on top (config_aot)"));
    parser.addOption(skinOption);
    parser.addOption(nullAudioOption);
    parser.addOption(selfTestOption);
    parser.addOption(playOption);
    parser.addOption(enqueueOption);
    parser.addOption(noSingleInstanceOption);
    parser.addOption(alwaysOnTopOption);
    parser.addPositionalArgument("files", "Media files to play or enqueue", "[files...]");
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
    const QCommandLineOption soakHoldOption(QStringLiteral("soak-hold-ms"),
                                            QStringLiteral("Soak: hold each preset for N ms"),
                                            QStringLiteral("ms"));
    parser.addOption(soakPresetsOption);
    parser.addOption(soakHoldOption);
#endif
    parser.process(app);

    const QStringList inputFiles = parser.positionalArguments();
    QStringList forwardedArgs;
    if (!inputFiles.isEmpty()) {
        forwardedArgs << (parser.isSet(enqueueOption) ? QStringLiteral("--enqueue")
                                                      : QStringLiteral("--play"));
        forwardedArgs << inputFiles;
    }

    // CO-1: one process owns the playlist; later launches hand their files to
    // it and exit. Self-test must always construct its own windows.
    koalaamp::platform::SingleInstance singleInstance(singleInstanceName(), &app);
    QList<QStringList> pendingCommands;
    std::function<void(const QStringList&)> dispatchCommand;
    if (!parser.isSet(selfTestOption) && !parser.isSet(noSingleInstanceOption)) {
        const auto role = singleInstance.start(forwardedArgs);
        if (role == koalaamp::platform::SingleInstance::Role::Forwarded) {
            return 0;
        }
        if (role == koalaamp::platform::SingleInstance::Role::Failed) {
            std::fprintf(stderr, "koalaamp: single-instance socket unavailable\n");
        }
        QObject::connect(&singleInstance, &koalaamp::platform::SingleInstance::argumentsReceived,
                         &app, [&](const QStringList& args) {
                             if (dispatchCommand) {
                                 dispatchCommand(args);
                             } else {
                                 pendingCommands.append(args);
                             }
                         });
    }

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
        skinDb.loadFrom(dbPath); // missing file is not an error
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
        std::fprintf(stderr, "koalaamp: no skin loaded — pass --skin <file.wsz>\n"
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
        std::filesystem::path base = xdg.isEmpty()
                                         ? std::filesystem::path(home) / ".local" / "share"
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
        windowSet.setVisualizerVisible(true); // nothing renders while hidden
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
    config.output = parser.isSet(nullAudioOption)
                        ? koalaamp::playback::PlaybackConfig::Output::Null
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

    dispatchCommand = [&](const QStringList& args) {
        const bool enqueue = args.contains(QStringLiteral("--enqueue"));
        std::vector<std::string> paths;
        for (const QString& arg : args) {
            if (arg != QStringLiteral("--play") && arg != QStringLiteral("--enqueue")) {
                paths.push_back(arg.toStdString());
            }
        }
        if (paths.empty()) {
            window.show();
            window.raise();
            window.activateWindow();
            return;
        }
        const std::size_t first = playlist.entryCount();
        playlist.addMediaFiles(paths);
        playlist.show();
        playlist.raise();
        playlist.activateWindow();
        if (!enqueue && playlist.entryCount() > first) {
            playlist.playRow(first);
        }
    };
    if (!forwardedArgs.isEmpty()) {
        dispatchCommand(forwardedArgs);
    }
    for (const QStringList& queued : pendingCommands) {
        dispatchCommand(queued);
    }
    pendingCommands.clear();

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
    // SV-1: the connection manager's shared state. The book is identity only; the
    // access token lives in the OS keychain and is restored into activeServer.
    koalaamp::servers::jellyfin::CurlTransport serverTransport;
    const QString serverBookPath =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
        QStringLiteral("/servers.json");
    koalaamp::servers::ServerBook serverBook(serverBookPath.toStdString());
    serverBook.load();
    const QString libraryDbPath =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
        QStringLiteral("/library.db");
    const auto secretStore = koalaamp::servers::makePlatformSecretStore();

    // SV-5 / SV-6: the signed-in server. A keychain hit means Browse works before
    // the connection dialog is opened; a miss leaves the saved server for re-login.
    std::optional<koalaamp::servers::ServerAuth> activeServer =
        koalaamp::servers::restoreSession(serverBook, *secretStore);
    if (activeServer && serverBook.active() != nullptr) {
        std::fprintf(stderr, "koalaamp: restored %s session for %s\n",
                     serverBook.active()->backend == koalaamp::servers::ServerBackend::Plex
                         ? "Plex"
                         : "Jellyfin",
                     serverBook.active()->userName.c_str());
    }
#endif

#if defined(KOALAAMP_HAS_GL_VISUALIZER) && defined(KOALAAMP_HAS_PLAYBACK_CORE)
    // SV-2/SV-4: report the signed-in session's playback. Jellyfin uses the
    // Sessions/Playing reporter; Plex uses /:/timeline. The timer keeps the
    // heartbeat independent of position ticks (which stop while paused).
    struct ServerReporting {
        koalaamp::playback::PlaybackCore* core{nullptr};
        std::unique_ptr<koalaamp::servers::jellyfin::JellyfinClient> jellyfinClient;
        std::unique_ptr<koalaamp::servers::jellyfin::JellyfinPlaybackReporter> jellyfinReporter;
        std::unique_ptr<koalaamp::servers::plex::PlexPlaybackReporter> plexReporter;

        ServerReporting() {
            timer.setInterval(1000);
            QObject::connect(&timer, &QTimer::timeout, &timer, [this] {
                const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count();
                if (jellyfinReporter) {
                    jellyfinReporter->tick(now);
                }
                if (plexReporter) {
                    plexReporter->tick(now);
                }
            });
        }
        ~ServerReporting() { detach(); }

        void attach(koalaamp::playback::PlaybackCore& playback,
                    koalaamp::servers::jellyfin::IHttpTransport& http,
                    const koalaamp::servers::ServerAuth& auth) {
            detach();
            core = &playback;
            if (auth.backend == koalaamp::servers::ServerBackend::Plex) {
                koalaamp::servers::plex::PlexClientInfo info{auth.deviceId, "KoalaAmp", "0.0.1",
                                                             "KoalaAmp", "Generic"};
                koalaamp::servers::plex::PlexSession session{
                    auth.baseUrl, auth.accessToken, auth.serverId, auth.userId, auth.userName};
                plexReporter = std::make_unique<koalaamp::servers::plex::PlexPlaybackReporter>(
                    http, std::move(info), std::move(session));
                core->addObserver(plexReporter.get());
            } else {
                koalaamp::servers::jellyfin::DeviceProfile profile;
                profile.deviceName = koalaamp::servers::jellyfin::defaultDeviceName();
                jellyfinClient =
                    std::make_unique<koalaamp::servers::jellyfin::JellyfinClient>(http, profile);
                jellyfinReporter =
                    std::make_unique<koalaamp::servers::jellyfin::JellyfinPlaybackReporter>(
                        *jellyfinClient, auth.baseUrl, auth.accessToken, auth.deviceId);
                core->addObserver(jellyfinReporter.get());
            }
            timer.start();
        }

        /// The id the Jellyfin planner must embed in a transcode URL.
        std::string playSessionId() const {
            return jellyfinReporter ? jellyfinReporter->playSessionId() : std::string{};
        }

        void detach() {
            timer.stop();
            if (core != nullptr) {
                if (jellyfinReporter) {
                    core->removeObserver(jellyfinReporter.get());
                }
                if (plexReporter) {
                    core->removeObserver(plexReporter.get());
                }
            }
            jellyfinReporter.reset();
            jellyfinClient.reset();
            plexReporter.reset();
            core = nullptr;
        }

        QTimer timer;
    } serverReporting;

    if (activeServer) {
        serverReporting.attach(*core, serverTransport, *activeServer);
    }

    // SC-1: client-side scrobbling. The coordinator is synchronous; the service
    // owns a worker so a slow Last.fm/ListenBrainz call never blocks playback.
    // Mode is per active server (or "local"), default Off per spec §8.4.
    ScrobbleSecretAdapter scrobbleSecrets{*secretStore};
    const std::string lastFmKey = lastFmApiKey();
    const std::string lastFmSecret = lastFmSharedSecret();
    const std::string lastFmSessionKey = koalaamp::scrobble::loadLastFmSessionKey(scrobbleSecrets);
    const std::string listenBrainzToken =
        koalaamp::scrobble::loadListenBrainzToken(scrobbleSecrets);

    const auto scrobbleModeKey =
        [](const std::optional<koalaamp::servers::ServerAuth>& session) -> QString {
        if (!session) {
            return QStringLiteral("local");
        }
        return QStringLiteral("%1/%2")
            .arg(session->backend == koalaamp::servers::ServerBackend::Plex
                     ? QStringLiteral("plex")
                     : QStringLiteral("jellyfin"))
            .arg(QString::fromStdString(session->serverId));
    };
    QSettings scrobbleSettings;
    std::atomic<int> scrobbleMode{
        scrobbleSettings
            .value(QStringLiteral("scrobble/mode/") + scrobbleModeKey(activeServer),
                   static_cast<int>(koalaamp::scrobble::ScrobbleMode::Off))
            .toInt()};
    auto scrobbleTransport = koalaamp::scrobble::makeCurlTransport();
    koalaamp::scrobble::ScrobbleService scrobbleService{
        *scrobbleTransport,
        (QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
         QStringLiteral("/scrobble-queue.json"))
            .toStdString(),
        static_cast<koalaamp::scrobble::ScrobbleMode>(scrobbleMode.load()),
        koalaamp::scrobble::LastFmCredentials{lastFmKey, lastFmSecret},
        lastFmSessionKey,
        listenBrainzToken};

    const auto refreshScrobbleMode = [&] {
        QSettings settings;
        const int mode =
            settings
                .value(QStringLiteral("scrobble/mode/") + scrobbleModeKey(activeServer),
                       static_cast<int>(koalaamp::scrobble::ScrobbleMode::Off))
                .toInt();
        scrobbleMode.store(mode);
        scrobbleService.setMode(static_cast<koalaamp::scrobble::ScrobbleMode>(mode));
    };

    struct ScrobbleWiring {
        koalaamp::playback::PlaybackCore* core{nullptr};
        koalaamp::scrobble::PlaybackScrobbler observer;

        ScrobbleWiring(koalaamp::playback::PlaybackCore& playback,
                       koalaamp::scrobble::ScrobbleService& service,
                       koalaamp::scrobble::PlaybackScrobbler::ModeProvider mode,
                       koalaamp::scrobble::PlaybackScrobbler::TrackResolver resolve)
            : core(&playback), observer(service, std::move(mode), std::move(resolve)) {
            core->addObserver(&observer);
        }
        ~ScrobbleWiring() {
            if (core != nullptr) {
                core->removeObserver(&observer);
            }
        }
    } scrobbleWiring{
        *core, scrobbleService,
        [&scrobbleMode] {
            return static_cast<koalaamp::scrobble::ScrobbleMode>(scrobbleMode.load());
        },
        [](const koalaamp::playback::MediaRef& media) -> std::optional<koalaamp::library::Track> {
            if (media.track) {
                return media.track;
            }
            if (media.kind == koalaamp::playback::MediaRef::Kind::Path) {
                if (auto scanned = koalaamp::library::readTrack(media.value)) {
                    return scanned->track;
                }
            }
            return std::nullopt;
        }};

    // LI-3: the local library window and its playback counters. The window owns
    // a read connection; this observer has a second connection so playcount and
    // lastplayed are written even when the window is closed.
    struct LibraryPlaybackStats final : koalaamp::playback::IPlaybackObserver {
        koalaamp::playback::PlaybackCore* core{nullptr};
        koalaamp::library::LibraryDb db;
        std::string currentPath;

        LibraryPlaybackStats(koalaamp::playback::PlaybackCore& playback, std::string path)
            : core(&playback), db(std::move(path)) {
            db.open();
            // Existing libraries predate the event log; seed it once from each
            // track's latest lastplayed so the History node is not empty.
            db.backfillHistoryFromTracks();
            core->addObserver(this);
        }
        ~LibraryPlaybackStats() override {
            if (core != nullptr) {
                core->removeObserver(this);
            }
        }
        void onTrackChanged(const koalaamp::playback::MediaRef& media) override {
            currentPath = media.kind == koalaamp::playback::MediaRef::Kind::Path ? media.value
                                                                                 : std::string{};
        }
        void onEndReached(const koalaamp::playback::MediaRef&) override {
            if (!currentPath.empty()) {
                db.markPlayed(currentPath, static_cast<std::int64_t>(std::time(nullptr)));
            }
        }
    } libraryStats{*core, libraryDbPath.toStdString()};

    QPointer<koalaamp::ui::LibraryWindow> libraryWindow;
    QPointer<koalaamp::ui::ServerBrowserWindow> serverBrowser;
    std::unique_ptr<koalaamp::ui::IServerBrowserSource> browserSource;

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
        const auto& auth = *activeServer;
        if (auth.backend == koalaamp::servers::ServerBackend::Plex) {
            koalaamp::servers::plex::PlexClientInfo info{auth.deviceId, "KoalaAmp", "0.0.1",
                                                         "KoalaAmp", "Generic"};
            browserSource = std::make_unique<koalaamp::ui::PlexBrowserSource>(
                serverTransport, std::move(info),
                koalaamp::servers::plex::PlexSession{auth.baseUrl, auth.accessToken, auth.serverId,
                                                     auth.userId, auth.userName});
        } else {
            koalaamp::servers::jellyfin::DeviceProfile profile;
            profile.deviceName = koalaamp::servers::jellyfin::defaultDeviceName();
            browserSource = std::make_unique<koalaamp::ui::JellyfinBrowserSource>(serverTransport,
                                                                                  profile, auth);
        }
        serverBrowser = new koalaamp::ui::ServerBrowserWindow(
            *browserSource,
            [&](const std::vector<koalaamp::library::Track>& tracks, std::size_t start,
                bool enqueueNext) {
                if (!activeServer || tracks.empty() || start >= tracks.size()) {
                    return;
                }
                const auto& session = *activeServer;
                if (session.backend == koalaamp::servers::ServerBackend::Plex) {
                    koalaamp::servers::plex::PlexClientInfo info{session.deviceId, "KoalaAmp",
                                                                 "0.0.1", "KoalaAmp", "Generic"};
                    const auto make = [&](const koalaamp::library::Track& track) {
                        return koalaamp::servers::plex::makePlexMedia(
                            serverTransport, info, session.baseUrl, session.accessToken, track);
                    };
                    if (enqueueNext) {
                        core->enqueueNext(make(tracks[start]));
                        return;
                    }
                    std::vector<koalaamp::playback::MediaRef> list;
                    list.reserve(tracks.size() - start);
                    for (std::size_t i = start; i < tracks.size(); ++i) {
                        list.push_back(make(tracks[i]));
                    }
                    core->playList(std::move(list), 0);
                    return;
                }
                koalaamp::servers::jellyfin::DeviceProfile profile;
                profile.deviceName = koalaamp::servers::jellyfin::defaultDeviceName();
                koalaamp::servers::jellyfin::JellyfinPlayback planner{serverTransport, profile};
                const std::string sessionId = serverReporting.playSessionId();
                if (enqueueNext) {
                    auto plan = planner.plan(session.baseUrl, session.accessToken, session.deviceId,
                                             tracks[start], sessionId);
                    core->enqueueNext(std::move(plan.media));
                    return;
                }
                std::vector<koalaamp::playback::MediaRef> list;
                list.reserve(tracks.size() - start);
                for (std::size_t i = start; i < tracks.size(); ++i) {
                    auto plan = planner.plan(session.baseUrl, session.accessToken, session.deviceId,
                                             tracks[i], sessionId);
                    list.push_back(std::move(plan.media));
                }
                core->playList(std::move(list), 0);
            },
            &window);
        serverBrowser->setAttribute(Qt::WA_DeleteOnClose);
        serverBrowser->setSkin(skinSet ? &*skinSet : nullptr);
        // A stale token sends the user back to the connection manager rather
        // than leaving a browser that answers every click with 401.
        QObject::connect(
            serverBrowser, &koalaamp::ui::ServerBrowserWindow::sessionExpired, &window, [&] {
                if (serverBrowser) {
                    serverBrowser->close();
                }
                if (activeServer) {
                    koalaamp::servers::forgetSession(*secretStore, activeServer->backend,
                                                     activeServer->serverId, activeServer->baseUrl);
                }
                activeServer.reset();
                browserSource.reset();
                serverReporting.detach();
                refreshScrobbleMode();
            });
        serverBrowser->show();
        serverBrowser->raise();
        serverBrowser->activateWindow();
    };
#endif

    const auto showMainMenu = [&] {
        QMenu menu;

#if defined(KOALAAMP_HAS_GL_VISUALIZER)
        // One entry point for the connection manager. Browse opens the library when
        // a keychain session was restored, and this dialog when it was not.
        const auto launchServersDialog = [&] {
            auto* dialog = new koalaamp::ui::ConnectionDialog(serverTransport, serverBook, {},
                                                              &window, secretStore.get());
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
                                 browserSource.reset();
                                 serverReporting.detach();
                                 if (activeServer) {
                                     serverReporting.attach(*core, serverTransport, *activeServer);
                                 }
                                 refreshScrobbleMode();
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
#if defined(KOALAAMP_HAS_PLAYBACK_CORE)
        QAction* library = menu.addAction(QStringLiteral("Media Library…"));
        QObject::connect(library, &QAction::triggered, &window, [&] {
            if (!libraryWindow) {
                libraryWindow = new koalaamp::ui::LibraryWindow(
                    libraryDbPath.toStdString(),
                    [&](const std::vector<koalaamp::library::Track>& tracks, std::size_t start) {
                        if (!core || tracks.empty() || start >= tracks.size()) {
                            return;
                        }
                        std::vector<koalaamp::playback::MediaRef> list;
                        list.reserve(tracks.size() - start);
                        for (std::size_t i = start; i < tracks.size(); ++i) {
                            koalaamp::playback::MediaRef media;
                            media.kind = koalaamp::playback::MediaRef::Kind::Path;
                            media.value = tracks[i].path;
                            media.track = tracks[i];
                            list.push_back(std::move(media));
                        }
                        core->playList(std::move(list), 0);
                    },
                    &window);
                libraryWindow->setAttribute(Qt::WA_DeleteOnClose);
                libraryWindow->setSkin(skinSet ? &*skinSet : nullptr);
            }
            libraryWindow->show();
            libraryWindow->raise();
            libraryWindow->activateWindow();
        });

        QAction* winampImport = menu.addAction(QStringLiteral("Import from Winamp…"));
        QObject::connect(winampImport, &QAction::triggered, &window, [&] {
            // The ML index is only a list of references; append every M3U it
            // points at through PL-1's readers rather than replacing the live
            // playlist, which the current-winamp.m3u option already handles.
            const auto importMlPlaylists = [&playlist](const std::string& indexPath) {
                std::ifstream in(std::filesystem::path(indexPath), std::ios::binary);
                if (!in) {
                    return false;
                }
                const std::string bytes{std::istreambuf_iterator<char>(in),
                                        std::istreambuf_iterator<char>()};
                const auto entries = koalaamp::playlist::readIndex(bytes);
                const std::filesystem::path base = std::filesystem::path(indexPath).parent_path();
                std::vector<koalaamp::ui::PleditEntry> rows;
                for (const auto& entry : entries) {
                    std::filesystem::path file(entry.filename);
                    if (file.is_relative()) {
                        file = base / file;
                    }
                    const auto list = koalaamp::playlist::readFile(file);
                    for (const auto& track : list.tracks) {
                        koalaamp::ui::PleditEntry row;
                        row.title = track.title.empty()
                                        ? koalaamp::playlist::titleFromFilename(track.filename)
                                        : track.title;
                        row.filename = track.filename;
                        row.lengthMs = track.lengthMs;
                        rows.push_back(std::move(row));
                    }
                }
                if (rows.empty()) {
                    return false;
                }
                playlist.addEntries(std::move(rows));
                return true;
            };
            const auto importBookmarks = [&playlist](const std::string& file) {
                const auto parsed = koalaamp::migration::readBookmarks(file);
                if (!parsed.ok()) {
                    return false;
                }
                std::vector<koalaamp::ui::PleditEntry> rows;
                rows.reserve(parsed.bookmarks.size());
                for (const auto& bookmark : parsed.bookmarks) {
                    koalaamp::ui::PleditEntry row;
                    row.title = bookmark.title;
                    row.filename = bookmark.filename;
                    rows.push_back(std::move(row));
                }
                if (rows.empty()) {
                    return false;
                }
                playlist.addEntries(std::move(rows));
                return true;
            };
            auto* dialog = new koalaamp::ui::MigrationDialog(
                libraryDbPath.toStdString(),
                [&playlist](const std::string& path) { return playlist.openPlaylist(path); },
                [&equalizer](const std::string& path) {
                    return equalizer.importPresetsFromFile(path);
                },
                &window);
            dialog->setIndexImporter(importMlPlaylists);
            dialog->setBookmarkImporter(importBookmarks);
            dialog->setSkinImporter([](const std::string& source) {
                const QString userSkins =
                    QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                    QStringLiteral("/skins");
                std::error_code error;
                std::filesystem::create_directories(std::filesystem::path(userSkins.toStdString()),
                                                    error);
                int copied = 0;
                for (const auto& entry :
                     std::filesystem::directory_iterator(std::filesystem::path(source), error)) {
                    if (!entry.is_regular_file(error)) {
                        continue;
                    }
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                        return static_cast<char>(std::tolower(ch));
                    });
                    if (ext != ".wsz") {
                        continue;
                    }
                    const std::filesystem::path destination =
                        std::filesystem::path(userSkins.toStdString()) / entry.path().filename();
                    if (!std::filesystem::exists(destination, error)) {
                        std::filesystem::copy_file(entry.path(), destination, error);
                        if (!error) {
                            ++copied;
                        }
                    }
                }
                return copied > 0;
            });
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setSkin(skinSet ? &*skinSet : nullptr);
            dialog->show();
            dialog->raise();
            dialog->activateWindow();
        });
#endif
        menu.addSeparator();
#if defined(KOALAAMP_HAS_PLAYBACK_CORE)
        QMenu* scrobbling = menu.addMenu(QStringLiteral("Scrobbling"));
        QActionGroup* scrobbleModes = new QActionGroup(scrobbling);
        scrobbleModes->setExclusive(true);
        const auto addScrobbleMode = [&](const QString& label,
                                         koalaamp::scrobble::ScrobbleMode mode) -> QAction* {
            QAction* action = scrobbling->addAction(label);
            action->setCheckable(true);
            action->setChecked(scrobbleMode.load() == static_cast<int>(mode));
            action->setActionGroup(scrobbleModes);
            QObject::connect(action, &QAction::triggered, &window, [&, mode] {
                QSettings settings;
                settings.setValue(QStringLiteral("scrobble/mode/") + scrobbleModeKey(activeServer),
                                  static_cast<int>(mode));
                scrobbleMode.store(static_cast<int>(mode));
                scrobbleService.setMode(mode);
                if (activeServer && mode == koalaamp::scrobble::ScrobbleMode::Client &&
                    !settings.value(QStringLiteral("scrobble/duplicateWarningSeen"), false)
                         .toBool()) {
                    settings.setValue(QStringLiteral("scrobble/duplicateWarningSeen"), true);
                    QMessageBox::information(
                        &window, QStringLiteral("Scrobbling"),
                        QStringLiteral("This app will submit listens for server tracks. "
                                       "Disable the server's Last.fm/Plex plugin, or choose "
                                       "Relay so only the server submits."));
                }
            });
            return action;
        };
        addScrobbleMode(QStringLiteral("Off"), koalaamp::scrobble::ScrobbleMode::Off);
        addScrobbleMode(QStringLiteral("Client (this app)"),
                        koalaamp::scrobble::ScrobbleMode::Client);
        QAction* relayMode = addScrobbleMode(QStringLiteral("Relay (server plugin)"),
                                             koalaamp::scrobble::ScrobbleMode::Relay);
        // Relay only makes sense with a server plugin; local playback has none.
        relayMode->setEnabled(activeServer.has_value());
        QAction* scrobbleHint =
            scrobbling->addAction(QStringLiteral("Only one side submits each listen"));
        scrobbleHint->setEnabled(false);
        scrobbling->addSeparator();

        const std::string existingLastFmUser =
            scrobbleSecrets.load(koalaamp::scrobble::kLastFmUsernameSlot).value_or(std::string{});
        const bool lastFmConfigured = !lastFmKey.empty() && !lastFmSecret.empty();
        QAction* lastFm = scrobbling->addAction(
            !lastFmConfigured ? QStringLiteral("Last.fm: app key not configured")
            : existingLastFmUser.empty()
                ? QStringLiteral("Connect Last.fm…")
                : QStringLiteral("Last.fm: %1").arg(QString::fromStdString(existingLastFmUser)));
        lastFm->setEnabled(lastFmConfigured);
        if (!lastFmConfigured) {
            lastFm->setToolTip(QStringLiteral(
                "This build has no Last.fm application key. The maintainer must register "
                "an API application and build with KOALAAMP_LASTFM_API_KEY / "
                "KOALAAMP_LASTFM_SHARED_SECRET; users cannot supply these."));
        }
        QObject::connect(lastFm, &QAction::triggered, &window, [&] {
            const std::string& apiKey = lastFmKey;
            const std::string& sharedSecret = lastFmSecret;
            if (apiKey.empty() || sharedSecret.empty()) {
                QMessageBox::information(
                    &window, QStringLiteral("Last.fm"),
                    QStringLiteral("This build has no Last.fm application key. ListenBrainz "
                                   "works with a personal token instead."));
                return;
            }

            auto authTransport = koalaamp::scrobble::makeCurlTransport();
            koalaamp::scrobble::LastFmClient client(
                koalaamp::scrobble::LastFmCredentials{apiKey, sharedSecret}, *authTransport);
            const auto token = client.getToken();
            if (!token) {
                const auto& error = client.lastError();
                QMessageBox::warning(
                    &window, QStringLiteral("Last.fm"),
                    error.message.empty()
                        ? QStringLiteral("Could not start Last.fm sign-in (code %1).")
                              .arg(error.code)
                        : QString::fromStdString(error.message));
                return;
            }
            QDesktopServices::openUrl(
                QUrl(QString::fromStdString(koalaamp::scrobble::lastFmAuthUrl(apiKey, *token))));
            QMessageBox::information(
                &window, QStringLiteral("Last.fm"),
                QStringLiteral("Authorize KoalaAmp in the browser, then click OK."));
            const auto session = client.getSession(*token);
            if (!session) {
                const auto& error = client.lastError();
                QMessageBox::warning(
                    &window, QStringLiteral("Last.fm"),
                    error.message.empty()
                        ? QStringLiteral("Last.fm did not return a session (code %1).")
                              .arg(error.code)
                        : QString::fromStdString(error.message));
                return;
            }
            if (!koalaamp::scrobble::rememberLastFmSession(scrobbleSecrets, session->name,
                                                           session->key)) {
                QMessageBox::warning(
                    &window, QStringLiteral("Last.fm"),
                    QStringLiteral("Connected, but the keychain refused the "
                                   "session token; it will not survive a restart."));
            }
            scrobbleService.setLastFm(koalaamp::scrobble::LastFmCredentials{apiKey, sharedSecret},
                                      session->key);
            QMessageBox::information(
                &window, QStringLiteral("Last.fm"),
                QStringLiteral("Connected as %1.").arg(QString::fromStdString(session->name)));
        });

        QAction* listenBrainz = scrobbling->addAction(QStringLiteral("ListenBrainz token…"));
        QObject::connect(listenBrainz, &QAction::triggered, &window, [&] {
            bool ok = false;
            const QString entered = QInputDialog::getText(
                &window, QStringLiteral("ListenBrainz"),
                QStringLiteral("User token (empty disconnects):"), QLineEdit::Password,
                QString::fromStdString(koalaamp::scrobble::loadListenBrainzToken(scrobbleSecrets)),
                &ok);
            if (!ok) {
                return;
            }
            if (entered.isEmpty()) {
                koalaamp::scrobble::forgetListenBrainzToken(scrobbleSecrets);
                scrobbleService.setListenBrainz(std::string{});
                return;
            }
            if (!koalaamp::scrobble::rememberListenBrainzToken(scrobbleSecrets,
                                                               entered.toStdString())) {
                QMessageBox::warning(&window, QStringLiteral("ListenBrainz"),
                                     QStringLiteral("The keychain refused the token."));
                return;
            }
            scrobbleService.setListenBrainz(entered.toStdString());
        });
        QAction* pending = scrobbling->addAction(
            QStringLiteral("%1 listen(s) queued").arg(scrobbleService.pendingCount()));
        pending->setEnabled(false);
#endif
#endif
        QMenu* presets = menu.addMenu(QStringLiteral("Presets"));
        QAction* choose = presets->addAction(QStringLiteral("Choose preset folder…"));
        QObject::connect(choose, &QAction::triggered, &window, [&] {
            const auto& roots = presetStore.roots();
            const QString start = roots.empty() ? QString() : QString::fromStdString(roots.front());
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
            current->setEnabled(false); // the name the overlay just announced
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
                return; // the bundled folder and the recent list can name one skin
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
        const QString userSkinDir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
            QStringLiteral("/skins");
        for (const auto& imported : koalaamp::skins::listSkins(userSkinDir.toStdString())) {
            addSkin(imported);
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
