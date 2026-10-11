// SPDX-License-Identifier: MIT
//
// SkinLoader — the concrete `ISkinLoader`. Given any path (a `.wsz`/`.zip`
// dropped onto a window, a recent-skins entry, an extracted folder) it produces
// a `SkinSet` or fails cleanly.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "skins/ISkinLoader.hpp"
#include "skins/SkinSet.hpp"

namespace koalaamp::skins {

class SkinLoader final : public ISkinLoader {
public:
    bool load(const std::filesystem::path& skin) override;

    SkinInfo info() const override;
    const SkinSet* skinSet() const override;

    /// Canonical members that were absent on the last successful load.
    const std::vector<std::string>& missingOptional() const;
    /// Recoverable problems recorded on the last successful load.
    const std::vector<std::string>& warnings() const;

    /// Clears the loaded skin.
    void reset() { m_skin.reset(); }

private:
    std::optional<SkinSet> m_skin;
};

}  // namespace koalaamp::skins
