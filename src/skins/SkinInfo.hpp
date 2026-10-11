// SPDX-License-Identifier: MIT
//
// SkinInfo — identifying metadata for a loaded skin. Deliberately Qt-free so the
// loader interface (ISkinLoader.hpp) stays usable without Qt.
#pragma once

#include <filesystem>
#include <string>

namespace koalaamp::skins {

struct SkinInfo {
    std::string name;             ///< Container file/folder name (e.g. "base-2.91.wsz").
    std::filesystem::path source; ///< Where it was loaded from.
    int mainWidth{0};
    int mainHeight{0};
};

}  // namespace koalaamp::skins
