// SPDX-License-Identifier: MIT
// LibraryWindow — the local media library (LI-3).
//
// Three panes inside the gen-window chrome: smart views/search/filters on top,
// an artist/album tree on the left and the track grid on the right. It owns a
// LibraryDb connection; scanning runs on a worker with its own connection so a
// long import never blocks the UI.

#pragma once

#include "library/LibraryDb.hpp"
#include "skins/SkinSet.hpp"
#include "ui/SkinnedWindow.hpp"

#include <QPoint>
#include <QSize>
#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <thread>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace koalaamp::ui {

class LibraryWindow final : public SkinnedWindow {
    Q_OBJECT

public:
    using PlayHandler =
        std::function<void(const std::vector<library::Track>& tracks, std::size_t startIndex)>;

    explicit LibraryWindow(std::string dbPath, PlayHandler play, QWidget* parent = nullptr);
    ~LibraryWindow() override;

    void setSkin(const skins::SkinSet* skin);
    QSize contentSize() const { return m_contentSize; }

protected:
    void paintEvent(QPaintEvent* event) override;
    bool isTitleBarAt(const QPoint& point) const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class NodeKind { All, Artist, Album };
    static constexpr int kNodeKindRole = Qt::UserRole;
    static constexpr int kNodeNameRole = Qt::UserRole + 1;
    static constexpr int kNodeAlbumArtistRole = Qt::UserRole + 2;
    static constexpr int kTrackIndexRole = Qt::UserRole + 10;
    static constexpr int kSortValueRole = Qt::UserRole + 20;

    void buildLayout();
    void applySkinColors();
    void reloadViews();
    void reloadArtists();
    void reloadFilters();
    void reloadTracks();
    void selectNode(QTreeWidgetItem* item);
    void expandArtist(QTreeWidgetItem* item);
    void fillTable(const std::vector<library::Track>& tracks, bool updateStatus = true);
    void rateRow(int row);
    void playRow(int row);
    void scanFolder();
    void refreshAll();
    void setStatus(const QString& text);
    bool closeHit(const QPoint& point) const;

    /// Parses a query string, or reports the syntax error inline.
    bool parseOrReport(const std::string& text, library::ParsedQuery& out);
    std::string baseWhere() const;
    std::string filterSuffix() const;

    std::string m_dbPath;
    PlayHandler m_play;
    library::LibraryDb m_db;
    const skins::SkinSet* m_skin = nullptr;
    QSize m_contentSize;

    QComboBox* m_views = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_genre = nullptr;
    QComboBox* m_year = nullptr;
    QPushButton* m_scan = nullptr;
    QPushButton* m_refresh = nullptr;
    QPushButton* m_saveView = nullptr;
    QTreeWidget* m_tree = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_status = nullptr;

    std::vector<library::Track> m_tracks;
    std::thread m_scanThread;
    std::atomic<bool> m_scanning{false};
    bool m_closePressed = false;
    bool m_loadingTree = false;
};

} // namespace koalaamp::ui
