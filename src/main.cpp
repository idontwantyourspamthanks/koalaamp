// SPDX-License-Identifier: MIT
// KoalaAmp application entry point.
//
// M0 scope was "construct the frameless toplevel"; SE-2 makes that toplevel the
// real thing: the classic main window drawn from a `.wsz` skin, wired to the
// playback engine (when the libvlc-backed core is built) through
// `ui::PlaybackCoreTransport` and its PCM tap.
//
// Usage: koalaamp [--skin <file.wsz|dir>] [--null-audio] [--self-test]
#include "ui/MainWindow.hpp"
#include "ui/PlaylistWindow.hpp"

#include "audio/AlbumEqStore.hpp"
#include "audio/DspPipeline.hpp"
#include "ui/DspEqSink.hpp"
#include "ui/EqWindow.hpp"
#include "ui/WindowPlatform.hpp"
#include "ui/WindowSet.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QString>
#include <QTimer>

#include "skins/SkinDb.hpp"
#include "skins/SkinSet.hpp"

#ifdef KOALAAMP_HAS_PLAYBACK_CORE
#include "playback/PlaybackCore.hpp"
#include "ui/PlaybackCoreTransport.hpp"
#endif

namespace {

/// Skin search order: an explicit path, then whatever the skin DB last used,
/// then the classic default in the (gitignored) fixture tree.
std::filesystem::path resolveSkin(const QString& explicitPath) {
    if (!explicitPath.isEmpty()) {
        return explicitPath.toStdString();
    }
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!configDir.isEmpty()) {
        koalaamp::skins::SkinDb db;
        if (db.loadFrom((configDir + QStringLiteral("/skindb.txt")).toStdString()) &&
            !db.current().empty()) {
            return db.current();
        }
    }
    return {};
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
    parser.addOption(skinOption);
    parser.addOption(nullAudioOption);
    parser.addOption(selfTestOption);
    parser.process(app);

    koalaamp::ui::MainWindow window;
    koalaamp::ui::PlaylistWindow playlist;
    koalaamp::ui::EqWindow equalizer;
    koalaamp::ui::WindowSet windowSet(window, equalizer, playlist);

    const std::filesystem::path skinPath = resolveSkin(parser.value(skinOption));
    std::optional<koalaamp::skins::SkinSet> skinSet;
    if (!skinPath.empty()) {
        skinSet = koalaamp::skins::loadSkinSet(skinPath);
        if (!skinSet.has_value()) {
            std::fprintf(stderr, "koalaamp: cannot load skin '%s'\n", skinPath.c_str());
        }
    }
    if (skinSet.has_value()) {
        window.setSkin(&*skinSet);
        playlist.setSkin(&*skinSet);
        equalizer.setSkin(&*skinSet);
        for (const auto& warning : skinSet->warnings) {
            std::fprintf(stderr, "koalaamp: skin: %s\n", warning.c_str());
        }
    } else {
        std::fprintf(stderr,
                     "koalaamp: no skin loaded — pass --skin <file.wsz>\n"
                     "          (the classic fixture is fetched by "
                     "spikes/fe3_wsz/fetch-skin.sh)\n");
    }

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
    auto eqSink = std::make_unique<koalaamp::ui::DspEqSink>(*dsp);
    auto albumEqStore = std::make_unique<koalaamp::audio::AlbumEqStore>();
    auto transport = std::make_unique<koalaamp::ui::PlaybackCoreTransport>(
        *core, window.visFeed(), dsp->makeCallback(window.visFeed().callback()));
    window.setTransport(transport.get());
    playlist.setTransport(transport.get());
    equalizer.setEqSink(eqSink.get());
    equalizer.setAlbumEqStore(albumEqStore.get());
#else
    (void)nullAudioOption;
#endif

    // The window set owns the inter-window toggles: the main window's EQ/PL
    // buttons show/hide their windows; each secondary close button hides it.
    // Winamp opens all three by default.
    windowSet.attach();
    windowSet.showAll();

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
