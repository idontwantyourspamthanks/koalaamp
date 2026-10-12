// SPDX-License-Identifier: MIT
//
// SE-5 spike: render the classic equalizer window from a `.wsz` so it can be
// pixel-diffed against Webamp's own render of the same skin. The renderer is
// `koalaamp::ui` (`renderEqWindow`) — the code the product paints with.
//
// Usage:
//   spike_se5 --skin <file.wsz> --out <render.png> [--state <name>] [--scale N]
//   spike_se5 --skin <file.wsz> --all <dir>          # every eq state, 1x and 2x
//   spike_se5 --skin <file.wsz> --list-states
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <QImage>

#include "skins/SkinSet.hpp"
#include "ui/EqGoldenStates.hpp"
#include "ui/EqRenderer.hpp"
#include "ui/EqState.hpp"

namespace {

using koalaamp::ui::EqWindowState;

void usage() {
    std::fputs("usage: spike_se5 --skin <file.wsz> [--out <png>] [--state <name>] [--scale N]\n"
               "                [--all <dir>] [--list-states]\n",
               stderr);
}

}  // namespace

int main(int argc, char** argv) {
    std::string skinPath = SE5_DEFAULT_SKIN;
    std::string out;
    std::string allDir;
    std::string stateName = "eq_default";
    int scale = 1;
    bool listStates = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (arg == "--skin") {
            skinPath = next();
        } else if (arg == "--out") {
            out = next();
        } else if (arg == "--state") {
            stateName = next();
        } else if (arg == "--scale") {
            scale = std::stoi(next());
        } else if (arg == "--all") {
            allDir = next();
        } else if (arg == "--list-states") {
            listStates = true;
        } else {
            usage();
            return 2;
        }
    }

    if (listStates) {
        for (const auto& [name, state] : koalaamp::ui::eqGoldenStateMatrix()) {
            (void)state;
            std::puts(name.c_str());
        }
        return 0;
    }

    const auto skin = koalaamp::skins::loadSkinSet(skinPath);
    if (!skin.has_value()) {
        std::fprintf(stderr, "spike_se5: cannot load skin %s\n", skinPath.c_str());
        return 1;
    }

    if (!allDir.empty()) {
        std::filesystem::create_directories(allDir);
        for (const auto& [name, state] : koalaamp::ui::eqGoldenStateMatrix()) {
            for (const int s : {1, 2}) {
                const QImage image = koalaamp::ui::renderEqWindow(skin->sheets, state, s);
                const std::string path = allDir + "/" + name + "@" + std::to_string(s) + "x.png";
                if (!image.save(QString::fromStdString(path))) {
                    std::fprintf(stderr, "spike_se5: cannot write %s\n", path.c_str());
                    return 1;
                }
            }
        }
        return 0;
    }

    const EqWindowState* state = koalaamp::ui::eqGoldenState(stateName);
    if (state == nullptr) {
        std::fprintf(stderr, "spike_se5: unknown state %s\n", stateName.c_str());
        return 2;
    }
    const QImage image = koalaamp::ui::renderEqWindow(skin->sheets, *state, scale);
    if (!image.save(QString::fromStdString(out))) {
        std::fprintf(stderr, "spike_se5: cannot write %s\n", out.c_str());
        return 1;
    }
    return 0;
}
