// SPDX-License-Identifier: MIT
// skins — classic WSZ skin loading and sprite parsing (docs/spec.md §5).
#pragma once

#include <filesystem>

#include "skins/SkinInfo.hpp"

namespace koalaamp::skins {

class SkinSet;

/// Loads a classic skin container (ZIP or directory), tolerating missing
/// optional members (docs/spec.md §5.1). Implementations report failure via
/// `false` — never by throwing; the assembled model is available through
/// `skinSet()`.
class ISkinLoader {
public:
    virtual ~ISkinLoader() = default;

    virtual bool load(const std::filesystem::path& skin) = 0;
    virtual SkinInfo info() const = 0;
    /// The assembled `SkinSet` after a successful load, nullptr otherwise.
    virtual const SkinSet* skinSet() const = 0;
};

}  // namespace koalaamp::skins
