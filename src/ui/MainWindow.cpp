// SPDX-License-Identifier: MIT

#include "MainWindow.hpp"

namespace koalaamp::ui {

MainWindow::MainWindow(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setFixedSize(kWidth, kHeight);
    setWindowTitle(QStringLiteral("KoalaAmp"));
}

} // namespace koalaamp::ui
