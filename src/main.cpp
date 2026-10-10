// SPDX-License-Identifier: MIT
// KoalaAmp application entry point.
//
// M0 scope: construct a QApplication and show the single frameless toplevel
// that will become the main (player) window. No skin code, no audio engine.

#include "ui/MainWindow.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QString>
#include <QTimer>
#include <string_view>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("KoalaAmp"));
    QApplication::setApplicationVersion(QStringLiteral("0.0.1"));
    QApplication::setOrganizationName(QStringLiteral("KoalaAmp"));

    koalaamp::ui::MainWindow window;
    window.show();

    // Headless smoke hook used by ctest (QT_QPA_PLATFORM=offscreen) and local
    // verification: prove the window constructs and maps, then exit 0.
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--self-test") {
            QTimer::singleShot(250, &app, &QCoreApplication::quit);
            break;
        }
    }

    return app.exec();
}
