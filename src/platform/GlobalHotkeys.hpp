// SPDX-License-Identifier: MIT
// platform — global hotkeys (CO-1).
//
// QHotkey provides the native registration (RegisterHotKey / XGrabKey /
// Carbon); this wrapper owns the lifetime and reports failed bindings instead
// of silently losing them. Bindings are supplied by the app so the platform
// layer stays transport-agnostic.

#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <vector>

class QHotkey;

namespace koalaamp::platform {

struct HotkeyBinding {
    QString name;
    QKeySequence sequence;
    std::function<void()> action;
};

class GlobalHotkeys final : public QObject {
    Q_OBJECT

public:
    explicit GlobalHotkeys(QObject* parent = nullptr);
    ~GlobalHotkeys() override;

    /// Registers every binding. Returns the number that succeeded; failures are
    /// available via `failed()` (usually a reserved/duplicate X11 shortcut).
    int registerAll(const std::vector<HotkeyBinding>& bindings);
    void clear();

    const QStringList& failed() const { return m_failed; }

private:
    std::vector<std::unique_ptr<QHotkey>> m_hotkeys;
    QStringList m_failed;
};

} // namespace koalaamp::platform
