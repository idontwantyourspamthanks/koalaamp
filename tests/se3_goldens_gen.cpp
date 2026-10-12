// SPDX-License-Identifier: MIT
//
// SE-3 SE3 golden generator (developer tool, not a test): prints the playlist
// golden manifest consumed by tests/se3_render_test.cpp — one line per state
// and scale, `<name>@<scale>x <sha256>`.
//
//   cmake --build build/linux-gcc --target koalaamp_se3_goldens_gen
//   ./build/linux-gcc/tests/se3_goldens_gen base-2.91.wsz
//       > tests/fixtures/se3/goldens.sha256
//
// It needs the base skin (`spikes/fe3_wsz/fetch-skin.sh`) for its `pledit.bmp`
// tiles, and uses `pleditGoldenColors()` (no `Font=`) so the hash is the same
// on any machine regardless of installed system fonts.
#include <cstdio>
#include <filesystem>
#include <string>

#include <QGuiApplication>

#include "skins/SkinSet.hpp"
#include "ui/GoldenStates.hpp"
#include "ui/PleditGoldens.hpp"
#include "ui/PleditRenderer.hpp"

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: se3_goldens_gen <base-2.91.wsz>\n");
        return 2;
    }
    const auto skin = koalaamp::skins::loadSkinSet(std::filesystem::path(argv[1]));
    if (!skin.has_value()) {
        std::fprintf(stderr, "cannot load skin %s\n", argv[1]);
        return 1;
    }
    const auto colors = koalaamp::ui::pleditGoldenColors();
    for (const auto& [name, state] : koalaamp::ui::pleditGoldenMatrix()) {
        for (const int scale : {1, 2}) {
            const QImage image =
                koalaamp::ui::renderPleditWindow(skin->sheets, colors, state, scale);
            std::printf("%s@%dx %s\n", name.c_str(), scale,
                        koalaamp::ui::frameHash(image).c_str());
        }
    }
    return 0;
}
