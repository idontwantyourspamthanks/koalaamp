// SPDX-License-Identifier: MIT
// platform — see GlobalHotkeys.hpp.

#include "platform/GlobalHotkeys.hpp"

#include <QHotkey>

namespace koalaamp::platform {

GlobalHotkeys::GlobalHotkeys(QObject* parent) : QObject(parent) {}
GlobalHotkeys::~GlobalHotkeys() = default;

int GlobalHotkeys::registerAll(const std::vector<HotkeyBinding>& bindings) {
    clear();
    int registered = 0;
    for (const HotkeyBinding& binding : bindings) {
        auto hotkey = std::make_unique<QHotkey>(binding.sequence, true, this);
        if (!hotkey->isRegistered()) {
            m_failed.append(binding.name);
            continue;
        }
        if (binding.action) {
            QObject::connect(hotkey.get(), &QHotkey::activated, this, binding.action);
        }
        m_hotkeys.push_back(std::move(hotkey));
        ++registered;
    }
    return registered;
}

void GlobalHotkeys::clear() {
    m_hotkeys.clear();
    m_failed.clear();
}

} // namespace koalaamp::platform
