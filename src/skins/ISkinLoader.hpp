// SPDX-License-Identifier: MIT
// skins — classic WSZ skin loading and sprite parsing (docs/spec.md §5).
// Interface contract stub for FE-0; the loader is SE-1.

#pragma once

#include <filesystem>
#include <string>

namespace koalaamp::skins {

/// Identifying metadata for a loaded skin.
struct SkinInfo {
    std::string name;
    std::filesystem::path source;
    int mainWidth{0};
    int mainHeight{0};
};

/// Loads a classic skin container (ZIP or directory), tolerating missing
/// optional members (docs/spec.md §5.1). Implementations report failure via
/// `false` / a default-constructed SkinInfo — never by throwing.
class ISkinLoader {
public:
    virtual ~ISkinLoader() = default;

    virtual bool load(const std::filesystem::path& skin) = 0;
    virtual SkinInfo info() const = 0;
};

} // namespace koalaamp::skins
