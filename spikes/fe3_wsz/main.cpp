// SPDX-License-Identifier: MIT
//
// FE-3 spike: load a classic `.wsz`, decode its bitmaps, and render the main
// window with nearest-neighbour sprite blits so the result can be pixel-diffed
// against Webamp's render of the same skin.
//
// Usage:
//   spike_fe3 --skin <file.wsz> --out <render.png> [--state default|toggled]
//   spike_fe3 --skin <file.wsz> --dump-sheets <dir>   # decoded sheets -> PNG
//   spike_fe3 --skin <file.wsz> --info                # per-sheet BMP header facts
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include <QImage>

#include "skins/BmpDecoder.hpp"
#include "MainWindowRenderer.hpp"
#include "skins/Skin.hpp"
#include "skins/ZipReader.hpp"

namespace {

void usage() {
    std::fputs("usage: spike_fe3 --skin <file.wsz> [--out <png> | --dump-sheets <dir> | --info]\n"
               "                [--state default|toggled]\n",
               stderr);
}

}  // namespace

int main(int argc, char **argv) {
    std::string skinPath = FE3_DEFAULT_SKIN;
    std::string outPath;
    std::string dumpDir;
    std::string stateName = "default";
    bool info = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
        if (arg == "--skin") {
            skinPath = next();
        } else if (arg == "--out") {
            outPath = next();
        } else if (arg == "--dump-sheets") {
            dumpDir = next();
        } else if (arg == "--state") {
            stateName = next();
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

    if (info) {
        auto entries = koalaamp::skins::readZipFile(skinPath);
        if (!entries) {
            std::fprintf(stderr, "spike_fe3: cannot read '%s'\n", skinPath.c_str());
            return 1;
        }
        std::printf("%-16s %-11s %-6s %-12s %s\n", "sheet", "size", "bpp", "compression", "bytes");
        for (const auto &e : *entries) {
            const auto bi = koalaamp::skins::bmpInfo(e.data);
            if (!bi) {
                continue;
            }
            const char *comp = bi->compression == 0   ? "BI_RGB"
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

    const auto skin = koalaamp::skins::Skin::load(skinPath);
    if (!skin) {
        std::fprintf(stderr, "spike_fe3: failed to load skin '%s'\n", skinPath.c_str());
        return 1;
    }

    if (!dumpDir.empty()) {
        std::filesystem::create_directories(dumpDir);
        for (const auto &[name, image] : skin->sheets()) {
            const std::string file = dumpDir + "/" + name + ".png";
            if (!image.save(QString::fromStdString(file), "PNG")) {
                std::fprintf(stderr, "spike_fe3: failed to write %s\n", file.c_str());
                return 1;
            }
            std::printf("dumped %s (%dx%d)\n", file.c_str(), image.width(), image.height());
        }
        return 0;
    }

    fe3::MainWindowState state;
    if (stateName == "default") {
        // Webamp no-track startup state (see docs/spikes/fe3-wsz-render.md).
    } else if (stateName == "toggled") {
        state.shuffleSelected = true;
        state.repeatSelected = true;
        state.activeButton = "play";  // play button held down -> _ACTIVE sprite
    } else if (stateName == "playing") {
        state.status = fe3::MediaStatus::Playing;
        state.timeSeconds = 65;  // 01:05
    } else {
        std::fprintf(stderr, "spike_fe3: unknown state '%s'\n", stateName.c_str());
        return 2;
    }

    const QImage rendered = fe3::renderMainWindow(*skin, state);
    if (outPath.empty()) {
        outPath = "fe3_main_" + stateName + ".png";
    }
    if (!rendered.save(QString::fromStdString(outPath), "PNG")) {
        std::fprintf(stderr, "spike_fe3: failed to write '%s'\n", outPath.c_str());
        return 1;
    }
    std::printf("rendered %s (%dx%d, state=%s) from %s\n", outPath.c_str(), rendered.width(),
                rendered.height(), stateName.c_str(), skinPath.c_str());
    return 0;
}
