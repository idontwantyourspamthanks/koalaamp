// SPDX-License-Identifier: MIT
//
// FE-3/SE-2 spike: render the classic main window from a `.wsz` so it can be
// pixel-diffed against Webamp's own render of the same skin.
//
// The renderer is `koalaamp::ui` — the code the product paints with (SE-2
// superseded this spike's original renderer). Two ways to pick a state:
//   * `--state <name>`: one of the built-in states (see `--list-states`);
//   * `--from-json <reference-state.json> --state-name <name>`: render a state
//     captured from Webamp itself, so the diff compares like with like.
//
// Usage:
//   spike_fe3 --skin <file.wsz> --out <render.png> [--state <name>] [--scale N]
//   spike_fe3 --skin <file.wsz> --from-json <json> --state-name <name> --out <png>
//   spike_fe3 --skin <file.wsz> --all <dir>            # every state, 1x and 2x
//   spike_fe3 --skin <file.wsz> --dump-sheets <dir>    # decoded sheets -> PNG
//   spike_fe3 --skin <file.wsz> --info                 # per-sheet BMP header facts
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "skins/BmpDecoder.hpp"
#include "skins/SkinSet.hpp"
#include "skins/ZipReader.hpp"
#include "ui/GoldenStates.hpp"
#include "ui/MainWindow.hpp"
#include "ui/PlayerRenderer.hpp"
#include "ui/TitleText.hpp"
#include "ui/VisData.hpp"

namespace {

using koalaamp::skins::SkinSet;
using koalaamp::ui::PlayerState;
using koalaamp::ui::PlayerStatus;
using koalaamp::ui::VisMode;

void usage() {
    std::fputs("usage: spike_fe3 --skin <file.wsz> [--out <png> | --dump-sheets <dir> | --info]\n"
               "                [--state <name> | --from-json <json> --state-name <name>]\n"
               "                [--scale N] [--all <dir>] [--list-states] [--goldens]\n",
               stderr);
}

/// The state matrix is shared with the widget tests (`src/ui/GoldenStates`).
const std::vector<std::pair<std::string, PlayerState>>& builtinStates() {
    return koalaamp::ui::goldenStateMatrix();
}

std::optional<PlayerState> stateFromJson(const std::string& jsonPath, const std::string& name,
                                         std::string& error) {
    std::ifstream in(jsonPath);
    if (!in) {
        error = "cannot open " + jsonPath;
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(buffer.str()), &parseError);
    if (document.isNull() || !document.isObject()) {
        error = "invalid JSON: " + parseError.errorString().toStdString();
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    if (!root.contains(QString::fromStdString(name))) {
        error = "state '" + name + "' not present in " + jsonPath;
        return std::nullopt;
    }
    const QJsonObject entry = root.value(QString::fromStdString(name)).toObject();

    PlayerState state;
    const std::string status = entry.value("status").toString().toStdString();
    state.status = status == "playing"    ? PlayerStatus::Playing
                   : status == "paused"   ? PlayerStatus::Paused
                                          : PlayerStatus::Stopped;
    state.shade = entry.value("shade").toBool(false);
    state.selected = true;
    state.volume = entry.value("volume").toInt(78);
    state.balance = entry.value("balance").toInt(0);
    state.position = entry.value("positionPercent").toInt(0);
    state.shuffle = entry.value("shuffle").toBool(false);
    state.repeat = entry.value("repeat").toBool(false);
    state.channels = entry.value("channels").toInt(0);
    state.working = entry.value("working").toBool(false);
    // The captured title is the *display* string: padded to 31 cells for short
    // titles, or the `"  ***  "`-doubled form for scrolling ones. Reduce it to
    // the raw title the marquee logic takes, so our own layout reproduces the
    // captured frame exactly.
    std::string title = entry.value("title").toString().toStdString();
    const std::string separator = std::string(koalaamp::ui::kMarqueeSeparator);
    const auto doubled = title.find(separator);
    if (doubled != std::string::npos) {
        title.resize(doubled);  // both halves are identical by construction
    } else {
        while (!title.empty() && title.back() == ' ') {
            title.pop_back();
        }
    }
    state.title = title;
    state.marqueeOffsetPx = entry.value("marqueeOffsetPx").toInt(0);
    const int timeSeconds = entry.value("timeSeconds").toInt(-1);
    state.timeVisible = timeSeconds >= 0 && state.status != PlayerStatus::Stopped;
    state.timeSeconds = timeSeconds < 0 ? 0 : timeSeconds;
    const QString kbps = entry.value("kbps").toString();
    const QString khz = entry.value("khz").toString();
    state.kbpsKnown = !kbps.trimmed().isEmpty();
    state.kbps = kbps.trimmed().toInt();
    state.khzKnown = !khz.trimmed().isEmpty();
    state.khz = khz.trimmed().toInt();
    // The visualiser mode comes from the capture (`vis`), and Webamp's frames
    // come from a digital-silence stream: a spectrum is then the viscolor
    // background plus the dither dots and nothing else.
    const std::string vis = entry.value("vis").toString().toStdString();
    state.vis = vis == "oscilloscope" ? VisMode::Oscilloscope
                : vis == "spectrum"   ? VisMode::Spectrum
                                      : VisMode::Off;
    state.visFrame.clear();
    if (state.vis == VisMode::Spectrum) {
        state.visFrame.barsValid = true;
        state.visFrame.peaks.fill(koalaamp::ui::kPeakOff);
    }
    return state;
}

}  // namespace

int main(int argc, char** argv) {
    std::string skinPath = FE3_DEFAULT_SKIN;
    std::string outPath;
    std::string dumpDir;
    std::string allDir;
    std::string stateName = "default";
    std::string jsonPath;
    int scale = 1;
    bool info = false;
    bool listStates = false;
    bool goldens = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
        if (arg == "--skin") {
            skinPath = next();
        } else if (arg == "--out") {
            outPath = next();
        } else if (arg == "--dump-sheets") {
            dumpDir = next();
        } else if (arg == "--all") {
            allDir = next();
        } else if (arg == "--state") {
            stateName = next();
        } else if (arg == "--state-name") {
            stateName = next();
        } else if (arg == "--from-json") {
            jsonPath = next();
        } else if (arg == "--scale") {
            scale = std::stoi(next());
        } else if (arg == "--list-states") {
            listStates = true;
        } else if (arg == "--goldens") {
            goldens = true;
        } else if (arg == "--info") {
            info = true;
        } else if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "spike_fe3: unknown argument '%s'\n", arg.c_str());
            usage();
            return 2;
        }
    }

    if (listStates) {
        for (const auto& [name, state] : builtinStates()) {
            (void)state;
            std::printf("%s\n", name.c_str());
        }
        return 0;
    }

    if (info) {
        auto entries = koalaamp::skins::readZipFile(skinPath);
        if (!entries) {
            std::fprintf(stderr, "spike_fe3: cannot read '%s'\n", skinPath.c_str());
            return 1;
        }
        std::printf("%-16s %-11s %-6s %-12s %s\n", "sheet", "size", "bpp", "compression", "bytes");
        for (const auto& e : *entries) {
            const auto bi = koalaamp::skins::bmpInfo(e.data);
            if (!bi) {
                continue;
            }
            const char* comp = bi->compression == 0   ? "BI_RGB"
                               : bi->compression == 1 ? "BI_RLE8"
                               : bi->compression == 2 ? "BI_RLE4"
                                                      : "other";
            char size[24];
            std::snprintf(size, sizeof(size), "%dx%d", bi->width, bi->height);
            std::printf("%-16s %-11s %-6d %-12s %zu%s\n", e.basename.c_str(), size,
                        bi->bitsPerPixel, comp, e.data.size(), bi->topDown ? " (top-down)" : "");
        }
        return 0;
    }

    const auto skin = koalaamp::skins::loadSkinSet(skinPath);
    if (!skin) {
        std::fprintf(stderr, "spike_fe3: failed to load skin '%s'\n", skinPath.c_str());
        return 1;
    }
    const SkinSet& set = *skin;

    if (goldens) {
        // The golden manifest consumed by tests/se2_render_test.cpp: one line
        // per state and scale, `<name>@<scale>x <sha256>`.
        for (const auto& [name, state] : builtinStates()) {
            for (const int renderScale : {1, 2}) {
                const QImage image = koalaamp::ui::renderMainWindow(set.sheets, set.visColor,
                                                                   state, renderScale);
                std::printf("%s@%dx %s\n", name.c_str(), renderScale,
                            koalaamp::ui::frameHash(image).c_str());
            }
        }
        return 0;
    }

    if (!dumpDir.empty()) {
        std::filesystem::create_directories(dumpDir);
        for (const auto& [name, image] : set.sheets.sheets()) {
            const std::string file = dumpDir + "/" + name + ".png";
            if (!image.save(QString::fromStdString(file), "PNG")) {
                std::fprintf(stderr, "spike_fe3: failed to write %s\n", file.c_str());
                return 1;
            }
            std::printf("dumped %s (%dx%d)\n", file.c_str(), image.width(), image.height());
        }
        return 0;
    }

    if (!allDir.empty()) {
        std::filesystem::create_directories(allDir);
        for (const auto& [name, state] : builtinStates()) {
            for (const int renderScale : {1, 2}) {
                const QImage image = koalaamp::ui::renderMainWindow(set.sheets, set.visColor,
                                                                   state, renderScale);
                const std::string file =
                    allDir + "/" + name + (renderScale == 1 ? ".png" : "@2x.png");
                if (!image.save(QString::fromStdString(file), "PNG")) {
                    std::fprintf(stderr, "spike_fe3: failed to write %s\n", file.c_str());
                    return 1;
                }
                std::printf("rendered %s (%dx%d)\n", file.c_str(), image.width(), image.height());
            }
        }
        return 0;
    }

    std::optional<PlayerState> state;
    if (!jsonPath.empty()) {
        std::string error;
        state = stateFromJson(jsonPath, stateName, error);
        if (!state.has_value()) {
            std::fprintf(stderr, "spike_fe3: %s\n", error.c_str());
            return 2;
        }
    } else {
        const PlayerState* found = koalaamp::ui::goldenState(stateName);
        if (found == nullptr) {
            std::fprintf(stderr, "spike_fe3: unknown state '%s' (see --list-states)\n",
                         stateName.c_str());
            return 2;
        }
        state = *found;
    }

    const QImage rendered = koalaamp::ui::renderMainWindow(set.sheets, set.visColor, *state, scale);
    if (outPath.empty()) {
        outPath = "fe3_main_" + stateName + ".png";
    }
    if (!rendered.save(QString::fromStdString(outPath), "PNG")) {
        std::fprintf(stderr, "spike_fe3: failed to write '%s'\n", outPath.c_str());
        return 1;
    }
    std::printf("rendered %s (%dx%d, state=%s, scale=%d) from %s\n", outPath.c_str(),
                rendered.width(), rendered.height(), stateName.c_str(), scale, skinPath.c_str());
    return 0;
}
