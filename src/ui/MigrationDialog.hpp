// SPDX-License-Identifier: MIT
// MigrationDialog — the MI-1 three-step Winamp import (skinned gen window).
//
// Step 1 picks the Winamp folder, step 2 chooses what to bring across, step 3
// reports the NDE match counts and exports the unmatched paths. The actual
// playlist/preset work is delegated to the host callbacks so this dialog never
// owns the player's models.

#pragma once

#include "skins/SkinSet.hpp"
#include "ui/SkinnedWindow.hpp"

#include <QPoint>
#include <QSize>
#include <QString>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMouseEvent;
class QPushButton;

namespace koalaamp::ui {

class MigrationDialog final : public SkinnedWindow {
    Q_OBJECT

public:
    using PathCallback = std::function<bool(const std::string&)>;

    /// `openPlaylist` receives the current Winamp playlist; `importPresets`
    /// receives `winamp.q1`. Both return whether the host consumed the file.
    MigrationDialog(std::string databasePath, PathCallback openPlaylist, PathCallback importPresets,
                    QWidget* parent = nullptr);

    /// The Media Library index importer (PL-1's `playlists.xml` reader); set by
    /// the host because it appends to the live playlist window.
    void setIndexImporter(PathCallback importer) { m_importIndex = std::move(importer); }
    /// Copies classic `.wsz` skins from the Winamp folder into the app's skin
    /// folder (the Skins menu scans it on its next open).
    void setSkinImporter(PathCallback importer) { m_importSkins = std::move(importer); }

    void setSkin(const skins::SkinSet* skin);
    QSize contentSize() const { return m_contentSize; }

private:
    void paintEvent(QPaintEvent* event) override;
    bool isTitleBarAt(const QPoint& point) const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool closeHit(const QPoint& point) const;

    void showStep(int step);
    void chooseFolder();
    void nextStep();
    void runImport();
    void exportUnmatched();
    void setStatus(const QString& text);

    std::string m_databasePath;
    PathCallback m_openPlaylist;
    PathCallback m_importPresets;
    PathCallback m_importIndex;
    PathCallback m_importSkins;
    const skins::SkinSet* m_skin = nullptr;
    QSize m_contentSize;

    QLineEdit* m_path = nullptr;
    QPushButton* m_browse = nullptr;
    QPushButton* m_next = nullptr;
    QPushButton* m_back = nullptr;
    QPushButton* m_import = nullptr;
    QPushButton* m_export = nullptr;
    QPushButton* m_close = nullptr;
    QCheckBox* m_ratings = nullptr;
    QCheckBox* m_playlist = nullptr;
    QCheckBox* m_presets = nullptr;
    QCheckBox* m_mlPlaylists = nullptr;
    QCheckBox* m_skins = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_report = nullptr;
    QListWidget* m_unmatchedList = nullptr;

    int m_step = 0;
    bool m_closePressed = false;
    std::optional<std::string> m_playlistPath;
    std::optional<std::string> m_presetPath;
    std::optional<std::string> m_indexPath;
    std::optional<std::string> m_skinsPath;
    std::vector<std::string> m_unmatched;
};

} // namespace koalaamp::ui
