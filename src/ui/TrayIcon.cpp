// SPDX-License-Identifier: MIT
// TrayIcon — see TrayIcon.hpp.

#include "ui/TrayIcon.hpp"

#include "ui/Transport.hpp"
#include "ui/WindowSet.hpp"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>

namespace koalaamp::ui {

TrayIcon::TrayIcon(WindowSet& windows, QObject* parent) : QObject(parent), m_windows(windows) {
    m_menu = new QMenu;
    m_playPause = m_menu->addAction(QStringLiteral("Play"));
    m_previous = m_menu->addAction(QStringLiteral("Previous"));
    m_next = m_menu->addAction(QStringLiteral("Next"));
    m_menu->addSeparator();
    m_showHide = m_menu->addAction(QStringLiteral("Hide windows"));
    m_menu->addSeparator();
    m_quit = m_menu->addAction(QStringLiteral("Quit"));

    QObject::connect(m_playPause, &QAction::triggered, this, [this] {
        if (m_transport != nullptr) {
            m_transport->togglePause();
        }
    });
    QObject::connect(m_previous, &QAction::triggered, this, [this] {
        if (m_transport != nullptr) {
            m_transport->previous();
        }
    });
    QObject::connect(m_next, &QAction::triggered, this, [this] {
        if (m_transport != nullptr) {
            m_transport->next();
        }
    });
    QObject::connect(m_showHide, &QAction::triggered, this, &TrayIcon::toggleWindows);
    QObject::connect(m_quit, &QAction::triggered, this, &TrayIcon::quit);
    QObject::connect(m_menu, &QMenu::aboutToShow, this, &TrayIcon::refresh);

    QIcon icon = QApplication::windowIcon();
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);
    }
    m_tray = new QSystemTrayIcon(icon, this);
    m_tray->setToolTip(QStringLiteral("KoalaAmp"));
    m_tray->setContextMenu(m_menu);
    QObject::connect(m_tray, &QSystemTrayIcon::activated, this,
                     [this](QSystemTrayIcon::ActivationReason reason) {
                         if (reason == QSystemTrayIcon::DoubleClick) {
                             toggleWindows();
                         }
                     });
    refresh();
}

TrayIcon::~TrayIcon() {
    hide();
    delete m_menu;
}

void TrayIcon::setTransport(ITransport* transport) {
    m_transport = transport;
    refresh();
}

bool TrayIcon::isAvailable() const {
    return m_tray != nullptr && QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayIcon::show() {
    if (isAvailable()) {
        m_tray->show();
    }
}

void TrayIcon::hide() {
    if (m_tray != nullptr) {
        m_tray->hide();
    }
}

void TrayIcon::refresh() {
    const bool hasTransport = m_transport != nullptr;
    m_playPause->setEnabled(hasTransport);
    m_previous->setEnabled(hasTransport);
    m_next->setEnabled(hasTransport);
    if (hasTransport && m_transport->state() == playback::PlaybackState::Playing) {
        m_playPause->setText(QStringLiteral("Pause"));
    } else {
        m_playPause->setText(QStringLiteral("Play"));
    }
    m_showHide->setText(m_windows.anyVisible() ? QStringLiteral("Hide windows")
                                               : QStringLiteral("Show windows"));
}

void TrayIcon::toggleWindows() {
    m_windows.setVisible(!m_windows.anyVisible());
    refresh();
}

void TrayIcon::quit() {
    QCoreApplication::quit();
}

} // namespace koalaamp::ui
