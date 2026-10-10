// SPDX-License-Identifier: MIT
#pragma once

#include <QWidget>

namespace koalaamp::ui {

/// The main (player) window.
///
/// M0 skeleton: a frameless 275x116 toplevel — the exact geometry of the
/// classic Winamp 2 main window (docs/spec.md §5.1). Skin rendering and the
/// real window set arrive with SE-1/SE-2; this exists so every later ticket
/// has a runnable target to hang work off.
class MainWindow : public QWidget {
    Q_OBJECT

public:
    static constexpr int kWidth = 275;
    static constexpr int kHeight = 116;

    explicit MainWindow(QWidget* parent = nullptr);
};

} // namespace koalaamp::ui
