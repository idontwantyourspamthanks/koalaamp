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
    auto transport =
        std::make_unique<koalaamp::ui::PlaybackCoreTransport>(*core, window.visFeed());
    window.setTransport(transport.get());
#else
    (void)nullAudioOption;
#endif

    window.show();

    if (parser.isSet(selfTestOption)) {
        // Headless smoke hook used by ctest (QT_QPA_PLATFORM=offscreen): prove
        // the window constructs and maps, then exit 0.
        QTimer::singleShot(250, &app, &QCoreApplication::quit);
    }

    const int status = app.exec();
#ifdef KOALAAMP_HAS_PLAYBACK_CORE
    window.setTransport(nullptr);
#endif
    return status;
}
