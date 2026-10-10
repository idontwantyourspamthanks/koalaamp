// SPDX-License-Identifier: MIT
// FE-1 spike — masked frameless toplevel + QOpenGLWidget child.
//
// Question (docs/tickets/FE-1-masked-gl-window.md): does QWidget::setMask coexist
// with a GL swapchain child in one frameless toplevel on Win/mac/Linux, and does
// the mask clip pixels and input without a compositor running?
//
// This program is a *probe*, not app code. It prints machine-readable lines to
// stderr prefixed `SPIKE|` so the driver script (x11_matrix.sh / manual runs on
// Windows/macOS) can assert on them. No WCL/GPL source was consulted.

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPolygonF>
#include <QRegion>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWindow>
#include <cmath>
#include <cstdio>

namespace {

constexpr int kWidth = 275; // classic Winamp main-window geometry (spec §5.1)
constexpr int kHeight = 116;

void logLine(const QString& line) {
    std::fprintf(stderr, "SPIKE|%s\n", qUtf8Printable(line));
    std::fflush(stderr);
}

// Ray-casting point-in-polygon. This is exactly the app-level hit-test the spec
// requires for macOS (where the mask is a visual CAShapeLayer only) and for
// Wayland (where setMask is a platform no-op).
bool pointInPolygon(const QPolygonF& poly, const QPointF& p) {
    bool inside = false;
    for (int i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const QPointF& a = poly[i];
        const QPointF& b = poly[j];
        const bool straddles = ((a.y() > p.y()) != (b.y() > p.y()));
        if (!straddles) continue;
        const double x = a.x() + (p.y() - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
        if (p.x() < x) inside = !inside;
    }
    return inside;
}

QPolygonF starPolygon() {
    // Five-point star, point up, inscribed in the kWidth x kHeight box. Outer
    // radius 55, inner radius 28 — a deliberately non-rectangular, spiky mask so
    // clipping is unambiguous (ticket: "star / L-shape, not a rectangle").
    QPolygonF poly;
    const QPointF c(kWidth / 2.0, kHeight / 2.0);
    const double outer = 55.0, inner = 28.0;
    for (int i = 0; i < 10; ++i) {
        const double r = (i % 2 == 0) ? outer : inner;
        const double a = M_PI_2 + i * M_PI / 5.0; // start at top
        poly << QPointF(c.x() + r * std::cos(a), c.y() - r * std::sin(a));
    }
    return poly;
}

QPolygonF lShapePolygon() {
    // An L: full-width top bar (y < 45) + left column (x < 80). Non-convex.
    return QPolygonF{QPointF(0, 0),   QPointF(275, 0),  QPointF(275, 45),
                     QPointF(80, 45), QPointF(80, 116), QPointF(0, 116)};
}

// Spinning triangle rendered through QOpenGLShaderProgram inside the child
// QOpenGLWidget. Rotation is driven from the toplevel's frame timer.
class SpinTriangle : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit SpinTriangle(QWidget* parent = nullptr) : QOpenGLWidget(parent) {}

    quint64 frameCount() const { return frameCount_; }

    void setAngle(float deg) { angle_ = deg; }

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        glClearColor(0.05f, 0.05f, 0.20f, 1.0f); // distinct from window bg

        program_ = new QOpenGLShaderProgram(this);
        program_->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                          "#version 330 core\n"
                                          "layout(location=0) in vec2 pos;\n"
                                          "uniform float angle;\n"
                                          "void main(){\n"
                                          "  float c = cos(angle), s = sin(angle);\n"
                                          "  vec2 p = vec2(c*pos.x - s*pos.y, s*pos.x + c*pos.y);\n"
                                          "  gl_Position = vec4(p, 0.0, 1.0);\n"
                                          "}\n");
        program_->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                          "#version 330 core\n"
                                          "out vec4 frag;\n"
                                          "void main(){ frag = vec4(1.0, 0.65, 0.1, 1.0); }\n");
        program_->link();

        const float verts[] = {0.0f, 0.85f, -0.74f, -0.6f, 0.74f, -0.6f};
        vao_.create();
        vao_.bind();
        vbo_.create();
        vbo_.bind();
        vbo_.allocate(verts, sizeof(verts));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        vbo_.release();
        vao_.release();

        logLine(
            QStringLiteral("gl=initialized vendor=%1 renderer=%2 version=%3")
                .arg(QString::fromUtf8(reinterpret_cast<const char*>(glGetString(GL_VENDOR))),
                     QString::fromUtf8(reinterpret_cast<const char*>(glGetString(GL_RENDERER))),
                     QString::fromUtf8(reinterpret_cast<const char*>(glGetString(GL_VERSION)))));
    }

    void resizeGL(int w, int h) override { glViewport(0, 0, w, h); }

    void paintGL() override {
        ++frameCount_;
        glViewport(0, 0, width(), height());
        glClear(GL_COLOR_BUFFER_BIT);
        if (program_ && program_->isLinked()) {
            program_->bind();
            program_->setUniformValue("angle", angle_);
            vao_.bind();
            glDrawArrays(GL_TRIANGLES, 0, 3);
            vao_.release();
            program_->release();
        }
    }

    quint64 frameCount_ = 0;

private:
    QOpenGLShaderProgram* program_ = nullptr;
    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    float angle_ = 0.0f;
};

class SpikeWindow : public QWidget {
public:
    SpikeWindow(const QString& which, int frameIntervalMs, const QSize& resizeTo = {})
        : poly_(which == QLatin1String("lshape") ? lShapePolygon() : starPolygon()) {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        if (resizeTo.isValid())
            resize(resizeTo);
        else
            setFixedSize(kWidth, kHeight);
        setWindowTitle(QStringLiteral("spike_fe1"));

        // Non-rectangular mask — the whole point of the spike.
        setMask(QRegion(poly_.toPolygon()));

        // GL child: a square near the centre. On the star it deliberately pokes
        // past the arms so we can see the mask clip GL output too.
        const int side = 84;
        gl_ = new SpinTriangle(this);
        gl_->setGeometry(QRect((kWidth - side) / 2, 8, side, side));

        timer_.setInterval(frameIntervalMs);
        connect(&timer_, &QTimer::timeout, this, [this] {
            angle_ += 3.0f;
            gl_->update();
        });
        timer_.start();
    }

    // A local point guaranteed inside the mask, used for input/pixel probes.
    QPoint centerLocal() const {
        return poly_.size() == 6 ? QPoint(30, 30) : QPoint(kWidth / 2, kHeight / 2);
    }

    // A local point guaranteed outside the mask, used by the input-clipping test.
    QPoint outsideLocal() const { return poly_.size() == 6 ? QPoint(268, 108) : QPoint(3, 3); }

    quint64 frames() const { return gl_->frameCount(); }

    void printEnvironment() {
        logLine(
            QStringLiteral("platform=%1 desktop=%2")
                .arg(QGuiApplication::platformName(), QGuiApplication::desktopFileName().isEmpty()
                                                          ? QStringLiteral("-")
                                                          : QGuiApplication::desktopFileName()));
        logLine(QStringLiteral("mask_polygon=%1 points=%2 region_bounds=%3x%4")
                    .arg(poly_.size() == 6 ? QStringLiteral("lshape") : QStringLiteral("star"))
                    .arg(poly_.size())
                    .arg(mask().boundingRect().width())
                    .arg(mask().boundingRect().height()));

        const QPoint origin = mapToGlobal(QPoint(0, 0));
        logLine(QStringLiteral("geom x=%1 y=%2 w=%3 h=%4 winid=0x%5")
                    .arg(origin.x())
                    .arg(origin.y())
                    .arg(width())
                    .arg(height())
                    .arg(static_cast<qulonglong>(winId()), 0, 16));
        const QPoint in = centerLocal();
        const QPoint out = outsideLocal();
        logLine(QStringLiteral("points inside_local=%1,%2 inside_abs=%3,%4 "
                               "outside_local=%5,%6 outside_abs=%7,%8")
                    .arg(in.x())
                    .arg(in.y())
                    .arg(origin.x() + in.x())
                    .arg(origin.y() + in.y())
                    .arg(out.x())
                    .arg(out.y())
                    .arg(origin.x() + out.x())
                    .arg(origin.y() + out.y()));
        const QScreen* scr = screen();
        logLine(QStringLiteral("screen=%1 %2x%3 dpr=%4 refresh=%5")
                    .arg(scr ? scr->name() : QStringLiteral("?"))
                    .arg(scr ? scr->size().width() : 0)
                    .arg(scr ? scr->size().height() : 0)
                    .arg(scr ? scr->devicePixelRatio() : 0.0)
                    .arg(scr ? scr->refreshRate() : 0.0));
    }

    // Synthesise a press at a local point and hand it to this window, bypassing
    // the platform's input routing. Used where a real pointer cannot be injected
    // (native Wayland without ydotool) to exercise the app-level hit-test path.
    void injectPress(const QPoint& local) {
        const QPoint global = mapToGlobal(local);
        QMouseEvent ev(QEvent::MouseButtonPress, QPointF(local), QPointF(global), Qt::LeftButton,
                       Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(this, &ev);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        // Magenta background: pixels that survive to screen outside the polygon
        // are a clipping failure and are trivially visible in a screenshot.
        QPainter p(this);
        p.fillRect(rect(), QColor(255, 0, 255));
    }

    void mousePressEvent(QMouseEvent* ev) override {
        const QPointF local = ev->position();
        const bool hit = pointInPolygon(poly_, local);
        logLine(QStringLiteral("press local=%1,%2 hit=%3")
                    .arg(qRound(local.x()))
                    .arg(qRound(local.y()))
                    .arg(hit ? QStringLiteral("ACCEPT") : QStringLiteral("REJECT")));
    }

private:
    QPolygonF poly_;
    SpinTriangle* gl_ = nullptr;
    QTimer timer_;
    float angle_ = 0.0f;
};

} // namespace

int main(int argc, char** argv) {
    // Must be set before the first GL widget is constructed (ticket: "QSurfaceFormat
    // compatible with the raster surface"). Core 3.3 works on all three platforms.
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("spike_fe1"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("FE-1 masked-window + GL spike"));
    parser.addHelpOption();
    const QCommandLineOption polyOpt(QStringLiteral("polygon"),
                                     QStringLiteral("Mask polygon: star|lshape"),
                                     QStringLiteral("shape"), QStringLiteral("star"));
    const QCommandLineOption durOpt(
        QStringLiteral("duration"),
        QStringLiteral("Run for N seconds then print fps and exit (0 = wait for close)"),
        QStringLiteral("sec"), QStringLiteral("0"));
    const QCommandLineOption moveOpt(QStringLiteral("move"),
                                     QStringLiteral("X11: move the toplevel to x,y before mapping"),
                                     QStringLiteral("x,y"));
    const QCommandLineOption frameOpt(
        QStringLiteral("frame-ms"),
        QStringLiteral("Frame timer interval in ms (0 = as fast as possible)"),
        QStringLiteral("ms"), QStringLiteral("0"));
    const QCommandLineOption injectOpt(
        QStringLiteral("inject-click"),
        QStringLiteral("Synthesise presses at inside+outside points (Wayland fallback)"));
    const QCommandLineOption resizeOpt(
        QStringLiteral("resize"),
        QStringLiteral("Resize the toplevel to WxH after mapping (mask stays 275x116)"),
        QStringLiteral("WxH"));
    parser.addOptions({polyOpt, durOpt, moveOpt, frameOpt, injectOpt, resizeOpt});
    parser.process(app);

    QSize resizeTo;
    if (parser.isSet(resizeOpt)) {
        const QStringList wh = parser.value(resizeOpt).split(QLatin1Char('x'));
        if (wh.size() == 2) resizeTo = QSize(wh[0].toInt(), wh[1].toInt());
    }
    SpikeWindow window(parser.value(polyOpt), parser.value(frameOpt).toInt(), resizeTo);

    if (parser.isSet(moveOpt)) {
        const QStringList xy = parser.value(moveOpt).split(QLatin1Char(','));
        if (xy.size() == 2) window.move(xy[0].toInt(), xy[1].toInt());
    }

    window.show();

    // Give the platform a moment to map the window before we interrogate it.
    QTimer::singleShot(400, &window, [&window] { window.printEnvironment(); });

    if (resizeTo.isValid()) {
        QTimer::singleShot(900, &window, [&window, resizeTo] {
            window.resize(resizeTo);
            logLine(QStringLiteral("resized to %1x%2 (mask unchanged)")
                        .arg(resizeTo.width())
                        .arg(resizeTo.height()));
        });
    }

    if (parser.isSet(injectOpt)) {
        QTimer::singleShot(700, &window, [&window] {
            logLine(QStringLiteral("injecting at inside point"));
            window.injectPress(window.centerLocal());
            logLine(QStringLiteral("injecting at outside point"));
            window.injectPress(window.outsideLocal());
        });
    }

    const int seconds = parser.value(durOpt).toInt();
    if (seconds > 0) {
        auto* clock = new QElapsedTimer;
        clock->start();
        QTimer::singleShot(seconds * 1000, &app, [&window, clock] {
            const qint64 ms = clock->elapsed();
            const quint64 frames = window.frames();
            logLine(QStringLiteral("fps frames=%1 elapsed_ms=%2 avg=%3")
                        .arg(frames)
                        .arg(ms)
                        .arg(ms > 0 ? QString::number(frames * 1000.0 / ms, 'f', 1)
                                    : QStringLiteral("nan")));
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
