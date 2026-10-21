// SPDX-License-Identifier: MIT
// TrayIcon — the system-tray transport and window-set verbs (CO-1).
//
// The tray is created only in the primary process. It is inert when the
// platform has no tray (offscreen CI, some Wayland sessions) so the app keeps
// working unchanged there.

#pragma once

#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace koalaamp::ui {

class ITransport;
class WindowSet;

class TrayIcon final : public QObject {
    Q_OBJECT

public:
    explicit TrayIcon(WindowSet& windows, QObject* parent = nullptr);
    ~TrayIcon() override;

    void setTransport(ITransport* transport);
    bool isAvailable() const;
    /// The tray context menu (exposed for scripted tests).
    QMenu* menu() const { return m_menu; }

    void show();
    void hide();

private:
    void refresh();
    void toggleWindows();
    void quit();

    WindowSet& m_windows;
    ITransport* m_transport = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_menu = nullptr;
    QAction* m_playPause = nullptr;
    QAction* m_previous = nullptr;
    QAction* m_next = nullptr;
    QAction* m_showHide = nullptr;
    QAction* m_quit = nullptr;
};

} // namespace koalaamp::ui
