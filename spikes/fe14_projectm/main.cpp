// SPDX-License-Identifier: MIT
// FE-14 spike entry point — hosts the projectM VizCanvas in either a masked or
// an unmasked frameless toplevel, drives the preset cycler, captures a
// screenshot of the X root (so XShape clipping is actually visible), samples
// RSS, and writes a JSON metrics report. Throwaway spike code.
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPalette>
#include <QPolygon>
#include <QRegion>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

#include "VizCanvas.hpp"

#ifndef FE14_DEFAULT_PRESET_DIR
#define FE14_DEFAULT_PRESET_DIR ""
#endif

namespace {

QStringList collectPresets(const QString& path) {
    QStringList files;
    const QFileInfo info(path);
    if (info.isDir()) {
        const QDir dir(path);
        for (const QFileInfo& f :
             dir.entryInfoList({"*.milk", "*.milk2"}, QDir::Files, QDir::Name)) {
            files << f.absoluteFilePath();
        }
    } else if (info.isFile()) {
        files << info.absoluteFilePath();
    }
    return files;
}

// A deliberately non-rectangular 5-point star over the widget rect, so XShape
// clipping (or its absence) is unambiguous.
QRegion starMask(const QSize& size) {
    QPolygon poly;
    const double cx = size.width() / 2.0;
    const double cy = size.height() / 2.0;
    const double outer = std::min(cx, cy);
    const double inner = outer * 0.45;
    for (int i = 0; i < 10; ++i) {
        const double angle = -M_PI / 2.0 + i * M_PI / 5.0;
        const double r = (i % 2 == 0) ? outer : inner;
        poly << QPoint(static_cast<int>(cx + r * std::cos(angle)),
                       static_cast<int>(cy + r * std::sin(angle)));
    }
    return QRegion(poly, Qt::WindingFill);
}

qint64 rssKb() {
#ifdef Q_OS_LINUX
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (statm.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> parts = statm.readAll().simplified().split(' ');
        if (parts.size() >= 2) {
            return parts[1].toLongLong() * (sysconf(_SC_PAGESIZE) / 1024);
        }
    }
#endif
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

#ifdef Q_OS_LINUX
    // The spike targets X11/Xvfb; the reference box is a Wayland session with
    // XWayland, where Qt otherwise picks the Wayland plugin (whose EGL path
    // fails under Xvfb). Default to xcb + GLX when nothing is set.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
        if (qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION")) {
            qputenv("QT_XCB_GL_INTEGRATION", "xcb_glx");
        }
    }
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("spike_fe14"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("FE-14 projectM-in-QOpenGLWidget spike"));
    parser.addHelpOption();
    const QCommandLineOption modeOpt(QStringLiteral("mode"),
                                     QStringLiteral("masked|unmasked toplevel"),
                                     QStringLiteral("mode"), QStringLiteral("masked"));
    const QCommandLineOption presetsOpt(
        QStringLiteral("presets"), QStringLiteral("preset file or directory"),
        QStringLiteral("path"), QString::fromLatin1(FE14_DEFAULT_PRESET_DIR));
    const QCommandLineOption widthOpt(QStringLiteral("width"), QStringLiteral("window width"),
                                      QStringLiteral("px"), QStringLiteral("1280"));
    const QCommandLineOption heightOpt(QStringLiteral("height"), QStringLiteral("window height"),
                                       QStringLiteral("px"), QStringLiteral("720"));
    const QCommandLineOption durationOpt(QStringLiteral("duration"),
                                         QStringLiteral("seconds to run (0 = until closed)"),
                                         QStringLiteral("s"), QStringLiteral("30"));
    const QCommandLineOption cycleOpt(QStringLiteral("cycle-ms"),
                                      QStringLiteral("preset cycle interval"), QStringLiteral("ms"),
                                      QStringLiteral("5000"));
    const QCommandLineOption reportOpt(QStringLiteral("report"), QStringLiteral("JSON report path"),
                                       QStringLiteral("file"));
    const QCommandLineOption captureOpt(
        QStringLiteral("capture"), QStringLiteral("X root screenshot path"), QStringLiteral("png"));
    const QCommandLineOption captureAtOpt(QStringLiteral("capture-at-ms"),
                                          QStringLiteral("when to capture"), QStringLiteral("ms"),
                                          QStringLiteral("3000"));
    const QCommandLineOption hardCutOpt(
        QStringLiteral("hard-cut"), QStringLiteral("use hard cuts instead of smooth transitions"));
    parser.addOptions({modeOpt, presetsOpt, widthOpt, heightOpt, durationOpt, cycleOpt, reportOpt,
                       captureOpt, captureAtOpt, hardCutOpt});
    parser.process(app);

    const QString mode = parser.value(modeOpt);
    const int width = std::max(64, parser.value(widthOpt).toInt());
    const int height = std::max(64, parser.value(heightOpt).toInt());
    const double duration = parser.value(durationOpt).toDouble();
    const int captureAt = parser.value(captureAtOpt).toInt();

    const QStringList presets = collectPresets(parser.value(presetsOpt));
    if (presets.isEmpty()) {
        std::fprintf(stderr, "FE-14 spike: no presets found at '%s'\n",
                     qPrintable(parser.value(presetsOpt)));
        return 2;
    }

    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(fmt);

    QWidget top;
    const bool masked = (mode != QLatin1String("unmasked"));
    if (masked) {
        top.setWindowFlags(Qt::FramelessWindowHint);
        top.setAutoFillBackground(true);
        QPalette pal = top.palette();
        pal.setColor(QPalette::Window, QColor(255, 0, 255)); // magenta: distinct from black root
        top.setPalette(pal);
    }
    top.setWindowTitle(QStringLiteral("FE-14 spike (%1)").arg(mode));

    auto* layout = new QVBoxLayout(&top);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* label = new QLabel(QStringLiteral("sibling widget (coexistence)"), &top);
    label->setStyleSheet(QStringLiteral("background:#203040; color:#c0d0e0; padding:2px;"));
    label->setAlignment(Qt::AlignCenter);
    auto* canvas = new fe14::VizCanvas(&top);
    layout->addWidget(label);
    layout->addWidget(canvas, 1);

    canvas->setCycleIntervalMs(parser.value(cycleOpt).toInt());
    canvas->setSmoothTransitions(!parser.isSet(hardCutOpt));
    canvas->setPresetFiles(presets);

    top.resize(width, height);
    if (masked) {
        top.setMask(starMask(top.size()));
    }
    top.show();

    // --- RSS sampling ---------------------------------------------------------
    qint64 rssMin = 0;
    qint64 rssMax = 0;
    qint64 rssLast = 0;
    int rssTicks = 0;
    QTimer rssTimer;
    QObject::connect(&rssTimer, &QTimer::timeout, [&] {
        const qint64 kb = rssKb();
        if (kb <= 0) {
            return;
        }
        rssLast = kb;
        rssMin = (rssMin == 0) ? kb : std::min(rssMin, kb);
        rssMax = std::max(rssMax, kb);
        if (++rssTicks % 30 == 0) {
            std::fprintf(stderr, "[fe14] rss t=%ds kB=%lld\n", rssTicks,
                         static_cast<long long>(kb));
        }
    });
    rssTimer.start(1000);

    // --- Screenshot of the X root (shows XShape clipping) ---------------------
    if (parser.isSet(captureOpt)) {
        const QString path = parser.value(captureOpt);
        QTimer::singleShot(std::max(0, captureAt), [path] {
            QScreen* screen = QGuiApplication::primaryScreen();
            if (screen != nullptr) {
                const QPixmap shot = screen->grabWindow(0);
                shot.save(path);
                std::printf("capture: %s (%dx%d)\n", qPrintable(path), shot.width(), shot.height());
            }
        });
    }

    // --- Run and report -------------------------------------------------------
    auto finish = [&] {
        canvas->stop();
        QJsonObject report = canvas->metrics();
        report["mode"] = mode;
        report["masked"] = masked;
        report["window_width"] = width;
        report["window_height"] = height;
        report["rss_kb_min"] = static_cast<double>(rssMin);
        report["rss_kb_max"] = static_cast<double>(rssMax);
        report["rss_kb_last"] = static_cast<double>(rssLast);
        report["rss_growth_kb"] = static_cast<double>(rssMax - rssMin);

        if (parser.isSet(reportOpt)) {
            QFile out(parser.value(reportOpt));
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                out.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
                out.close();
            }
        }

        std::printf("\n=== FE-14 spike summary (mode=%s) ===\n", qPrintable(mode));
        std::printf("gl: %s\n", qPrintable(canvas->glInfo()));
        std::printf("presets=%d cycle=%dms frames=%d avg_fps=%.2f\n",
                    static_cast<int>(presets.size()), parser.value(cycleOpt).toInt(),
                    static_cast<int>(report["frames"].toDouble()), report["avg_fps"].toDouble());
        std::printf("load: count=%d last=%.2fms avg=%.2fms\n",
                    static_cast<int>(report["load_count"].toDouble()),
                    report["last_load_ms"].toDouble(), report["avg_load_ms"].toDouble());
        std::printf("black: max_run=%.1fms max_switch=%.1fms switch_to_visible=%.1fms\n",
                    report["max_black_run_ms"].toDouble(), report["max_switch_black_ms"].toDouble(),
                    report["max_switch_to_visible_ms"].toDouble());
        std::printf("rss: min=%lldkB max=%lldkB growth=%lldkB\n", static_cast<long long>(rssMin),
                    static_cast<long long>(rssMax), static_cast<long long>(rssMax - rssMin));
        const QJsonArray failures = report["failed_presets"].toArray();
        for (const QJsonValue& f : failures) {
            std::printf("preset-failure: %s\n", qPrintable(f.toString()));
        }
        const QJsonArray logs = report["log_messages"].toArray();
        int shown = 0;
        for (const QJsonValue& l : logs) {
            if (++shown > 40) {
                std::printf("projectm-log: ... (%d total)\n", static_cast<int>(logs.size()));
                break;
            }
            std::printf("projectm-log: %s\n", qPrintable(l.toString()));
        }
        const QJsonArray presetArr = report["presets"].toArray();
        for (const QJsonValue& v : presetArr) {
            const QJsonObject p = v.toObject();
            std::printf("  %-40s load=%7.2fms fps=%7.2f frames=%6d mean_luma=%6.2f\n",
                        qPrintable(p["name"].toString()), p["load_ms"].toDouble(),
                        p["fps"].toDouble(), static_cast<int>(p["frames"].toDouble()),
                        p["mean_luma"].toDouble());
        }
        std::fflush(stdout);

        // Qt 6.10.2's XCB teardown path segfaults under Xvfb (QXcbWindow::
        // needsSync during QWidgetRepaintManager flush). All measurement is
        // already complete; exit without running Qt's destructors.
        std::_Exit(0);
    };

    if (duration > 0.0) {
        QTimer::singleShot(static_cast<int>(duration * 1000.0), &app, finish);
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, finish);

    return app.exec();
}
