// SPDX-License-Identifier: MIT
#include "skins/SkinLoader.hpp"

namespace koalaamp::skins {

bool SkinLoader::load(const std::filesystem::path& skin) {
    auto set = loadSkinSet(skin);
    if (!set) {
        m_skin.reset();
        return false;
    }
    m_skin = std::move(set);
    return true;
}

SkinInfo SkinLoader::info() const { return m_skin ? m_skin->info : SkinInfo{}; }

const SkinSet* SkinLoader::skinSet() const { return m_skin ? &*m_skin : nullptr; }

const std::vector<std::string>& SkinLoader::missingOptional() const {
    static const std::vector<std::string> kEmpty;
    return m_skin ? m_skin->missingOptional : kEmpty;
}

const std::vector<std::string>& SkinLoader::warnings() const {
    static const std::vector<std::string> kEmpty;
    return m_skin ? m_skin->warnings : kEmpty;
}

}  // namespace koalaamp::skins
