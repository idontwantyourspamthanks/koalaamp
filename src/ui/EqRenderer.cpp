// SPDX-License-Identifier: MIT
#include "ui/EqRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QPainter>

#include "skins/SkinSprites.hpp"

namespace koalaamp::ui {

using koalaamp::skins::Skin;

namespace {

/// Webamp `percentToRange(percent, min, max) = min + round(percent * (max-min))`.
int percentToRange(double percent, int min, int max) {
    return min + static_cast<int>(std::lround(percent * static_cast<double>(max - min)));
}

/// One RGB pixel of a 1-px-wide sprite strip, or black when the sheet/rect is
/// missing (an undersized skin renders a flat graph, like Winamp).
QColor stripColor(const Skin& skin, std::string_view sprite, int row) {
    const auto* sheet = skin.sheet("EQMAIN");
    const auto rect = skin.spriteRect("EQMAIN", sprite);
    if (sheet == nullptr || !rect || row < 0 || row >= rect->h) {
        return QColor(0, 0, 0);
    }
    return sheet->pixelColor(rect->x, rect->y + row);
}

// --- cubic spline (Webamp `js/components/EqualizerWindow/spline.js`, adapted
//     from morganherlocker/cubic-spline, MIT) ---------------------------------

std::vector<double> solveSpline(const std::vector<std::vector<double>>& matrixIn) {
    std::vector<std::vector<double>> matrix = matrixIn;
    const std::size_t m = matrix.size();
    std::vector<double> ks(m, 0.0);
    for (std::size_t k = 0; k < m; ++k) {
        std::size_t iMax = 0;
        double vali = -1e300;
        for (std::size_t i = k; i < m; ++i) {
            if (matrix[i][k] > vali) {
                iMax = i;
                vali = matrix[i][k];
            }
        }
        std::swap(matrix[k], matrix[iMax]);
        for (std::size_t i = k + 1; i < m; ++i) {
            for (std::size_t j = k + 1; j < m + 1; ++j) {
                matrix[i][j] -= matrix[k][j] * (matrix[i][k] / matrix[k][k]);
            }
            matrix[i][k] = 0.0;
        }
    }
    for (std::size_t ii = m; ii-- > 0;) {
        const double v = matrix[ii][m] / matrix[ii][ii];
        ks[ii] = v;
        for (std::size_t j = ii; j-- > 0;) {
            matrix[j][m] -= matrix[j][ii] * v;
            matrix[j][ii] = 0.0;
        }
    }
    return ks;
}

std::vector<double> naturalKs(const std::vector<double>& xs, const std::vector<double>& ys) {
    const std::size_t n = xs.size() - 1;
    std::vector<std::vector<double>> matrix(n + 1, std::vector<double>(n + 2, 0.0));
    for (std::size_t i = 1; i < n; ++i) {
        matrix[i][i - 1] = 1.0 / (xs[i] - xs[i - 1]);
        matrix[i][i] =
            2.0 * (1.0 / (xs[i] - xs[i - 1]) + 1.0 / (xs[i + 1] - xs[i]));
        matrix[i][i + 1] = 1.0 / (xs[i + 1] - xs[i]);
        matrix[i][n + 1] =
            3.0 * ((ys[i] - ys[i - 1]) / ((xs[i] - xs[i - 1]) * (xs[i] - xs[i - 1])) +
                   (ys[i + 1] - ys[i]) / ((xs[i + 1] - xs[i]) * (xs[i + 1] - xs[i])));
    }
    matrix[0][0] = 2.0 / (xs[1] - xs[0]);
    matrix[0][1] = 1.0 / (xs[1] - xs[0]);
    matrix[0][n + 1] = (3.0 * (ys[1] - ys[0])) / ((xs[1] - xs[0]) * (xs[1] - xs[0]));
    matrix[n][n - 1] = 1.0 / (xs[n] - xs[n - 1]);
    matrix[n][n] = 2.0 / (xs[n] - xs[n - 1]);
    matrix[n][n + 1] = (3.0 * (ys[n] - ys[n - 1])) / ((xs[n] - xs[n - 1]) * (xs[n] - xs[n - 1]));
    return solveSpline(matrix);
}

std::vector<double> spline(const std::vector<double>& xs, const std::vector<double>& ys) {
    const std::vector<double> ks = naturalKs(xs, ys);
    const int maxX = static_cast<int>(xs.back());
    std::vector<double> allYs;
    allYs.reserve(static_cast<std::size_t>(maxX) + 1);
    std::size_t i = 1;
    for (int x = 0; x <= maxX; ++x) {
        while (xs[i] < static_cast<double>(x)) {
            ++i;
        }
        const double t = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
        const double a = ks[i - 1] * (xs[i] - xs[i - 1]) - (ys[i] - ys[i - 1]);
        const double b = -ks[i] * (xs[i] - xs[i - 1]) + (ys[i] - ys[i - 1]);
        const double q = (1 - t) * ys[i - 1] + t * ys[i] + t * (1 - t) * (a * (1 - t) + b * t);
        allYs.push_back(q);
    }
    return allYs;
}

/// Webamp `EqGraph.drawEqLine`: the preamp marker line plus the band spline,
/// coloured by the 1x19 `EQ_GRAPH_LINE_COLORS` strip.
void drawEqGraph(const Skin& skin, QPainter& painter, const EqWindowState& state) {
    const QRect graph = EqLayout::graph;
    skin.blit(painter, "EQMAIN", "EQ_GRAPH_BACKGROUND", graph.x(), graph.y());

    constexpr int height = EqLayout::graph.height();  // 19
    constexpr int maxY = height - 1;                  // 18

    const int preamp = eqWebampValue(state.sliders[0]);
    const int preampY = std::clamp(percentToRange(preamp / 100.0, 0, maxY), 0, maxY);
    if (const auto* sheet = skin.sheet("EQMAIN")) {
        if (const auto rect = skin.spriteRect("EQMAIN", "EQ_PREAMP_LINE")) {
            painter.drawImage(QPoint(graph.x(), graph.y() + preampY), *sheet,
                              QRect(rect->x, rect->y, rect->w, rect->h));
        }
    }

    std::vector<double> xs;
    std::vector<double> ys;
    xs.reserve(kEqWindowBands);
    ys.reserve(kEqWindowBands);
    for (int i = 0; i < kEqWindowBands; ++i) {
        const int value = state.sliders[static_cast<std::size_t>(i + 1)];
        const double percent = (100.0 - static_cast<double>(eqWebampValue(value))) / 100.0;
        xs.push_back(static_cast<double>(i * 12));
        ys.push_back(static_cast<double>(percentToRange(percent, 0, maxY)));
    }
    const std::vector<double> allYs = spline(xs, ys);

    constexpr int paddingLeft = 2;
    int lastY = static_cast<int>(ys.front());
    for (int x = 0; x <= static_cast<int>(xs.back()); ++x) {
        const int y = std::clamp(static_cast<int>(std::lround(allYs[static_cast<std::size_t>(x)])),
                                 0, maxY);
        const int yTop = std::min(y, lastY);
        const int span = 1 + std::abs(lastY - y);
        for (int row = yTop; row < yTop + span; ++row) {
            painter.fillRect(graph.x() + paddingLeft + x, graph.y() + row, 1, 1,
                             stripColor(skin, "EQ_GRAPH_LINE_COLORS", row));
        }
        lastY = y;
    }
}

/// One slider (preamp or band): groove frame + 11x11 thumb.
void drawSlider(const Skin& skin, QPainter& painter, const EqWindowState& state, int sliderIndex) {
    const int x = eqBandX(sliderIndex);
    const int y = EqLayout::bandY;
    const int value = state.sliders[static_cast<std::size_t>(sliderIndex)];
    const int webamp = eqWebampValue(value);

    skin.blitRect(painter, "EQMAIN", eqGrooveSrcRect(webamp), x, y);
    const bool active = state.activeSlider == sliderIndex;
    skin.blit(painter, "EQMAIN", active ? "EQ_SLIDER_THUMB_SELECTED" : "EQ_SLIDER_THUMB",
              x + 1, y + eqThumbTop(webamp));
}

void drawEqWindowLayout(const Skin& skin, QPainter& painter, const EqWindowState& state) {
    skin.blit(painter, "EQMAIN", "EQ_WINDOW_BACKGROUND", 0, 0);
    skin.blit(painter, "EQMAIN",
              state.selected ? "EQ_TITLE_BAR_SELECTED" : "EQ_TITLE_BAR", 0, 0);

    // Window buttons live in the title-bar art while idle; only the held frame
    // is a separate sprite (Webamp `.clicked`). The shade/maximise button art
    // comes from `eq_ex.bmp` when present.
    if (state.closePressed) {
        skin.blit(painter, "EQMAIN", "EQ_CLOSE_BUTTON", EqLayout::closeButton.x(),
                  EqLayout::closeButton.y());
    }
    if (state.shadePressed) {
        if (!skin.blit(painter, "EQ_EX", "EQ_MAXIMIZE_BUTTON_ACTIVE", EqLayout::shadeButton.x(),
                       EqLayout::shadeButton.y())) {
            skin.blit(painter, "EQMAIN", "EQ_MAXIMIZE_BUTTON_ACTIVE_FALLBACK",
                      EqLayout::shadeButton.x(), EqLayout::shadeButton.y());
        }
    }

    // ON / AUTO: four states each.
    const auto pushButton = [](const char* idle, const char* selected, const char* depressed,
                               const char* depressedSelected, bool on, bool pressed) {
        if (pressed) {
            return on ? depressedSelected : depressed;
        }
        return on ? selected : idle;
    };
    {
        const char* sprite =
            pushButton("EQ_ON_BUTTON", "EQ_ON_BUTTON_SELECTED", "EQ_ON_BUTTON_DEPRESSED",
                       "EQ_ON_BUTTON_SELECTED_DEPRESSED", state.enabled, state.onPressed);
        skin.blit(painter, "EQMAIN", sprite, EqLayout::onButton.x(), EqLayout::onButton.y());
    }
    {
        const char* sprite =
            pushButton("EQ_AUTO_BUTTON", "EQ_AUTO_BUTTON_SELECTED", "EQ_AUTO_BUTTON_DEPRESSED",
                       "EQ_AUTO_BUTTON_SELECTED_DEPRESSED", state.autoLoad, state.autoPressed);
        skin.blit(painter, "EQMAIN", sprite, EqLayout::autoButton.x(), EqLayout::autoButton.y());
    }
    skin.blit(painter, "EQMAIN",
              state.presetsPressed ? "EQ_PRESETS_BUTTON_SELECTED" : "EQ_PRESETS_BUTTON",
              EqLayout::presetsButton.x(), EqLayout::presetsButton.y());

    drawEqGraph(skin, painter, state);

    drawSlider(skin, painter, state, 0);
    for (int band = 1; band < kEqWindowSliders; ++band) {
        drawSlider(skin, painter, state, band);
    }
}

/// Mini shade slider: the track art is baked into the windowshade background,
/// so only the 3x7 thumb is composited. Webamp picks the left/center/right
/// thumb sprite from the value's third of the range.
void drawShadeThumb(const Skin& skin, QPainter& painter, const char* sheet,
                    const char* leftSprite, const char* centerSprite, const char* rightSprite,
                    const QRect& track, int value, int minValue, int maxValue) {
    const double fraction =
        static_cast<double>(std::clamp(value, minValue, maxValue) - minValue) /
        static_cast<double>(maxValue - minValue);
    const int third = std::clamp(static_cast<int>(fraction * 3.0), 0, 2);
    const char* sprite = third == 0 ? leftSprite : third == 1 ? centerSprite : rightSprite;
    const int x = track.x() + eqShadeThumbLeft(value, minValue, maxValue, track.width());
    const int y = track.y();
    skin.blit(painter, sheet, sprite, x, y);
}

void drawEqShade(const Skin& skin, QPainter& painter, const EqWindowState& state) {
    const bool haveEx = skin.sheet("EQ_EX") != nullptr;
    if (haveEx) {
        skin.blit(painter, "EQ_EX",
                  state.selected ? "EQ_SHADE_BACKGROUND_SELECTED" : "EQ_SHADE_BACKGROUND", 0, 0);
    } else {
        // Webamp's fallback when a skin ships no eq_ex.bmp: the eqmain title bar.
        skin.blit(painter, "EQMAIN",
                  state.selected ? "EQ_TITLE_BAR_SELECTED" : "EQ_TITLE_BAR", 0, 0);
    }

    if (haveEx) {
        if (state.shadePressed) {
            skin.blit(painter, "EQ_EX", "EQ_MINIMIZE_BUTTON_ACTIVE", EqLayout::shadeButton.x(),
                      EqLayout::shadeButton.y());
        }
        if (state.closePressed) {
            skin.blit(painter, "EQ_EX", "EQ_SHADE_CLOSE_BUTTON", EqLayout::closeButton.x(),
                      EqLayout::closeButton.y());
        }
        drawShadeThumb(skin, painter, "EQ_EX", "EQ_SHADE_VOLUME_SLIDER_LEFT",
                       "EQ_SHADE_VOLUME_SLIDER_CENTER", "EQ_SHADE_VOLUME_SLIDER_RIGHT",
                       EqLayout::shadeVolume, state.volume, 0, 100);
        drawShadeThumb(skin, painter, "EQ_EX", "EQ_SHADE_BALANCE_SLIDER_LEFT",
                       "EQ_SHADE_BALANCE_SLIDER_CENTER", "EQ_SHADE_BALANCE_SLIDER_RIGHT",
                       EqLayout::shadeBalance, state.balance, -100, 100);
    } else if (state.closePressed) {
        skin.blit(painter, "EQMAIN", "EQ_CLOSE_BUTTON", EqLayout::closeButton.x(),
                  EqLayout::closeButton.y());
    }
}

}  // namespace

int eqWebampValue(int auValue) {
    const int clamped = std::clamp(auValue, kEqValueBoost, kEqValueCut);
    return static_cast<int>(std::lround((63.0 - static_cast<double>(clamped)) * 100.0 / 63.0));
}

int eqAuValueFromWebamp(int webampValue) {
    const int clamped = std::clamp(webampValue, 0, 100);
    const int value =
        static_cast<int>(std::lround(63.0 - static_cast<double>(clamped) * 63.0 / 100.0));
    return std::clamp(value, kEqValueBoost, kEqValueCut);
}

int eqGrooveFrame(int webampValue) {
    const int clamped = std::clamp(webampValue, 0, 100);
    const int frame = static_cast<int>(std::lround(clamped / 100.0 * (EqSliderMetrics::grooveFrames - 1)));
    return std::clamp(frame, 0, EqSliderMetrics::grooveFrames - 1);
}

QRect eqGrooveSrcRect(int webampValue) {
    const int frame = eqGrooveFrame(webampValue);
    const int col = frame % EqSliderMetrics::grooveCols;
    const int row = frame / EqSliderMetrics::grooveCols;
    return QRect(EqSliderMetrics::grooveSrcX + col * EqSliderMetrics::grooveCellW,
                 EqSliderMetrics::grooveSrcY + row * EqSliderMetrics::grooveCellH,
                 EqLayout::bandWidth, EqLayout::bandHeight);
}

int eqThumbTop(int webampValue) {
    const int clamped = std::clamp(webampValue, 0, 100);
    const int span = EqSliderMetrics::trackH - EqSliderMetrics::thumbH;  // 51
    return static_cast<int>(std::floor(span * (1.0 - clamped / 100.0)));
}

int eqShadeThumbLeft(int value, int minValue, int maxValue, int trackWidth) {
    const int clamped = std::clamp(value, minValue, maxValue);
    const double fraction = static_cast<double>(clamped - minValue) /
                            static_cast<double>(maxValue - minValue);
    return static_cast<int>(std::lround(fraction * (trackWidth - EqSliderMetrics::shadeThumbW)));
}

int eqBandX(int sliderIndex) {
    if (sliderIndex <= 0) {
        return EqLayout::preamp.x();
    }
    return EqLayout::bandX0 + EqLayout::bandPitch * (sliderIndex - 1);
}

QImage renderEqWindow(const Skin& skin, const EqWindowState& state, int scale) {
    const int baseHeight = state.shade ? kEqShadeHeight : kEqHeight;
    QImage canvas(kEqWidth, baseHeight, QImage::Format_RGB32);
    canvas.fill(Qt::black);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (state.shade) {
        drawEqShade(skin, painter, state);
    } else {
        drawEqWindowLayout(skin, painter, state);
    }
    painter.end();

    if (scale <= 1) {
        return canvas;
    }
    return canvas.scaled(kEqWidth * scale, baseHeight * scale, Qt::IgnoreAspectRatio,
                         Qt::FastTransformation);
}

}  // namespace koalaamp::ui
