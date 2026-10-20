// SPDX-License-Identifier: MIT
// MigrationDialog — see MigrationDialog.hpp.

#include "ui/MigrationDialog.hpp"

#include "migration/Migration.hpp"
#include "migration/WinampConfig.hpp"
#include "ui/GenRenderer.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace koalaamp::ui {
namespace {

using namespace koalaamp::migration;

const skins::Skin& noSheets() {
    static const skins::Skin empty;
    return empty;
}

const skins::PleditColors* pleditIfShipped(const skins::SkinSet* set) {
    if (set == nullptr) {
        return nullptr;
    }
    const auto& missing = set->missingOptional;
    if (std::find(missing.begin(), missing.end(), "pledit.txt") != missing.end()) {
        return nullptr;
    }
    return &set->pleditColors;
}

std::optional<std::string> firstExisting(const WinampPaths& paths,
                                         std::initializer_list<const char*> names) {
    for (const char* name : names) {
        const std::filesystem::path candidate = paths.playlistDir / name;
        std::error_code error;
        if (std::filesystem::exists(candidate, error)) {
            return candidate.string();
        }
        const std::filesystem::path fallback = paths.winampDir / name;
        if (std::filesystem::exists(fallback, error)) {
            return fallback.string();
        }
    }
    return std::nullopt;
}

} // namespace

MigrationDialog::MigrationDialog(std::string databasePath, PathCallback openPlaylist,
                                 PathCallback importPresets, QWidget* parent)
    : SkinnedWindow(parent), m_databasePath(std::move(databasePath)),
      m_openPlaylist(std::move(openPlaylist)), m_importPresets(std::move(importPresets)) {
    m_contentSize = genSnapSize(QSize(500, 350));
    setFixedSize(m_contentSize);
    const QRect client = genLayout(m_contentSize).client;
    const int x = client.x() + 8;
    const int width = client.width() - 16;

    m_path = new QLineEdit(this);
    m_path->setObjectName(QStringLiteral("winampFolder"));
    m_path->setPlaceholderText(QStringLiteral("Winamp folder (contains winamp.ini / main.dat)"));
    m_path->setGeometry(x, client.y() + 10, width - 92, 26);

    m_browse = new QPushButton(QStringLiteral("Browse"), this);
    m_browse->setObjectName(QStringLiteral("winampBrowse"));
    m_browse->setGeometry(x + width - 84, client.y() + 10, 84, 26);
    connect(m_browse, &QPushButton::clicked, this, &MigrationDialog::chooseFolder);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("winampStatus"));
    m_status->setWordWrap(true);
    m_status->setGeometry(x, client.y() + 44, width, 62);

    m_next = new QPushButton(QStringLiteral("Next"), this);
    m_next->setObjectName(QStringLiteral("winampNext"));
    m_next->setGeometry(x + width - 90, client.bottom() - 40, 90, 28);
    connect(m_next, &QPushButton::clicked, this, &MigrationDialog::nextStep);

    m_ratings = new QCheckBox(QStringLiteral("Ratings, play counts, last-played and dates"), this);
    m_ratings->setObjectName(QStringLiteral("winampRatings"));
    m_ratings->setChecked(true);
    m_ratings->setGeometry(x, client.y() + 14, width, 24);

    m_playlist = new QCheckBox(QStringLiteral("Current Winamp playlist (M3U/M3U8)"), this);
    m_playlist->setObjectName(QStringLiteral("winampPlaylist"));
    m_playlist->setChecked(true);
    m_playlist->setGeometry(x, client.y() + 42, width, 24);

    m_presets = new QCheckBox(QStringLiteral("Equalizer presets (winamp.q1)"), this);
    m_presets->setObjectName(QStringLiteral("winampPresets"));
    m_presets->setChecked(true);
    m_presets->setGeometry(x, client.y() + 70, width, 24);

    m_mlPlaylists = new QCheckBox(QStringLiteral("Media Library playlists (playlists.xml)"), this);
    m_mlPlaylists->setObjectName(QStringLiteral("winampMlPlaylists"));
    m_mlPlaylists->setChecked(true);
    m_mlPlaylists->setGeometry(x, client.y() + 98, width, 24);

    m_back = new QPushButton(QStringLiteral("Back"), this);
    m_back->setObjectName(QStringLiteral("winampBack"));
    m_back->setGeometry(x, client.bottom() - 40, 90, 28);
    connect(m_back, &QPushButton::clicked, this, [this] { showStep(0); });

    m_import = new QPushButton(QStringLiteral("Import"), this);
    m_import->setObjectName(QStringLiteral("winampImport"));
    m_import->setGeometry(x + width - 90, client.bottom() - 40, 90, 28);
    connect(m_import, &QPushButton::clicked, this, &MigrationDialog::runImport);

    m_report = new QLabel(this);
    m_report->setObjectName(QStringLiteral("winampReport"));
    m_report->setWordWrap(true);
    m_report->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_report->setGeometry(x, client.y() + 10, width, 88);

    m_unmatchedList = new QListWidget(this);
    m_unmatchedList->setObjectName(QStringLiteral("winampUnmatched"));
    m_unmatchedList->setGeometry(x, client.y() + 102, width, client.height() - 190);
    m_unmatchedList->setSelectionMode(QAbstractItemView::NoSelection);

    m_export = new QPushButton(QStringLiteral("Export unmatched…"), this);
    m_export->setObjectName(QStringLiteral("winampExport"));
    m_export->setGeometry(x, client.bottom() - 40, 140, 28);
    connect(m_export, &QPushButton::clicked, this, &MigrationDialog::exportUnmatched);

    m_close = new QPushButton(QStringLiteral("Close"), this);
    m_close->setObjectName(QStringLiteral("winampClose"));
    m_close->setGeometry(x + width - 90, client.bottom() - 40, 90, 28);
    connect(m_close, &QPushButton::clicked, this, &QWidget::close);

    // Start on the detected folder when there is one; otherwise the field is the
    // hint and the picker is the next step.
    const WinampPaths detected = detectWinampPaths({});
    if (detected.found()) {
        m_path->setText(QString::fromStdString(detected.winampDir.string()));
    }
    showStep(0);
}

void MigrationDialog::setSkin(const skins::SkinSet* skin) {
    m_skin = skin;
    setWindowShape(skin != nullptr ? skin->region : skins::Region{}, false, m_contentSize, 1);
    update();
}

void MigrationDialog::paintEvent(QPaintEvent*) {
    const skins::Skin& sheets = m_skin != nullptr ? m_skin->sheets : noSheets();
    const QImage chrome = renderGenWindow(sheets, pleditIfShipped(m_skin), m_contentSize,
                                          "WINAMP IMPORT", isActiveWindow(), 1, m_closePressed);
    QPainter painter(this);
    painter.drawImage(0, 0, chrome);
}

bool MigrationDialog::closeHit(const QPoint& point) const {
    return genLayout(m_contentSize).closeButton.contains(point);
}

bool MigrationDialog::isTitleBarAt(const QPoint& point) const {
    return genLayout(m_contentSize).title.contains(point) && !closeHit(point);
}

void MigrationDialog::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && closeHit(event->pos())) {
        m_closePressed = true;
        update();
        event->accept();
        return;
    }
    SkinnedWindow::mousePressEvent(event);
}

void MigrationDialog::mouseReleaseEvent(QMouseEvent* event) {
    if (m_closePressed) {
        const bool inside = closeHit(event->pos());
        m_closePressed = false;
        update();
        if (inside) {
            close();
        }
        event->accept();
        return;
    }
    SkinnedWindow::mouseReleaseEvent(event);
}

void MigrationDialog::showStep(int step) {
    m_step = step;
    m_path->setVisible(step == 0);
    m_browse->setVisible(step == 0);
    m_next->setVisible(step == 0);
    m_next->setText(QStringLiteral("Next"));
    m_status->setVisible(step == 0 || step == 1);
    m_status->setGeometry(m_status->x(),
                          step == 0 ? m_status->y() : genLayout(m_contentSize).client.bottom() - 70,
                          m_status->width(), 26);
    m_ratings->setVisible(step == 1);
    m_playlist->setVisible(step == 1);
    m_presets->setVisible(step == 1);
    m_mlPlaylists->setVisible(step == 1);
    m_back->setVisible(step == 1);
    m_import->setVisible(step == 1);
    m_report->setVisible(step == 2);
    m_unmatchedList->setVisible(step == 2);
    m_export->setVisible(step == 2);
    m_close->setVisible(step == 2);
}

void MigrationDialog::chooseFolder() {
    const QString dir =
        QFileDialog::getExistingDirectory(this, QStringLiteral("Winamp folder"), m_path->text());
    if (!dir.isEmpty()) {
        m_path->setText(dir);
    }
}

void MigrationDialog::nextStep() {
    if (m_step == 0) {
        const WinampPaths paths =
            detectWinampPaths(std::filesystem::path(m_path->text().toStdString()));
        if (!paths.found()) {
            setStatus(QStringLiteral("No winamp.ini or main.dat in that folder."));
            return;
        }
        m_playlistPath = firstExisting(paths, {"winamp.m3u8", "winamp.m3u", "winamp.pls"});
        m_presetPath.reset();
        m_indexPath.reset();
        const std::filesystem::path q1 = paths.winampDir / "winamp.q1";
        std::error_code error;
        if (std::filesystem::exists(q1, error)) {
            m_presetPath = q1.string();
        }
        const std::filesystem::path index =
            paths.winampDir / "plugins" / "ml" / "playlists" / "playlists.xml";
        if (std::filesystem::exists(index, error)) {
            m_indexPath = index.string();
        }
        const QString detail = QStringLiteral("Found %1. Choose what to bring across.")
                                   .arg(QString::fromStdString(paths.winampDir.string()));
        setStatus(detail);
        showStep(1);
        return;
    }
    if (m_step == 1) {
        runImport();
    }
}

void MigrationDialog::runImport() {
    const WinampPaths paths =
        detectWinampPaths(std::filesystem::path(m_path->text().toStdString()));
    if (!paths.found()) {
        setStatus(QStringLiteral("The Winamp folder disappeared."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    MigrationReport report;
    report.paths = paths;
    report.state = readWinampState(paths.iniFile);
    QString ratingsLine = QStringLiteral("Ratings: not selected");
    if (m_ratings->isChecked()) {
        MigrationOptions options;
        options.winampDir = paths.winampDir;
        options.databasePath = m_databasePath;
        report = runMigration(options);
        if (!report.ok()) {
            // A missing/corrupt NDE database must not block the playlist and
            // preset imports that share the same folder.
            ratingsLine = QStringLiteral("Ratings: %1").arg(QString::fromStdString(report.error));
        } else {
            ratingsLine = QStringLiteral("Library records read: %1 (matched %2)")
                              .arg(report.records)
                              .arg(report.matched);
        }
    }
    QApplication::restoreOverrideCursor();
    m_unmatched = report.unmatchedPaths;

    QString playlistLine = QStringLiteral("Playlist: not selected");
    if (m_playlist->isChecked() && m_playlistPath.has_value()) {
        const bool imported = m_openPlaylist != nullptr && m_openPlaylist(*m_playlistPath);
        playlistLine = imported ? QStringLiteral("Playlist: imported")
                                : QStringLiteral("Playlist: could not be read");
    } else if (m_playlist->isChecked()) {
        playlistLine = QStringLiteral("Playlist: none saved");
    }
    QString presetLine = QStringLiteral("EQ presets: not selected");
    if (m_presets->isChecked() && m_presetPath.has_value()) {
        const bool imported = m_importPresets != nullptr && m_importPresets(*m_presetPath);
        presetLine = imported ? QStringLiteral("EQ presets: imported")
                              : QStringLiteral("EQ presets: invalid");
    } else if (m_presets->isChecked()) {
        presetLine = QStringLiteral("EQ presets: none saved");
    }
    QString indexLine = QStringLiteral("ML playlists: not selected");
    if (m_mlPlaylists->isChecked() && m_indexPath.has_value()) {
        const bool imported = m_importIndex != nullptr && m_importIndex(*m_indexPath);
        indexLine = imported ? QStringLiteral("ML playlists: imported")
                             : QStringLiteral("ML playlists: none imported");
    } else if (m_mlPlaylists->isChecked()) {
        indexLine = QStringLiteral("ML playlists: none saved");
    }

    const QString text = QStringLiteral("%1\n"
                                        "Unmatched paths: %2\n"
                                        "Unknown fields skipped: %3\n"
                                        "Volume: %4 · EQ: %5\n\n%6\n%7\n%8")
                             .arg(ratingsLine)
                             .arg(report.unmatched)
                             .arg(report.skippedFields)
                             .arg(report.state.volume)
                             .arg(report.state.useEq ? QStringLiteral("on") : QStringLiteral("off"))
                             .arg(playlistLine, presetLine, indexLine);
    m_report->setText(text);
    m_unmatchedList->clear();
    for (const std::string& path : m_unmatched) {
        m_unmatchedList->addItem(QString::fromStdString(path));
    }
    m_unmatchedList->setVisible(!m_unmatched.empty());
    m_export->setEnabled(!m_unmatched.empty());
    showStep(2);
}

void MigrationDialog::exportUnmatched() {
    if (m_unmatched.empty()) {
        return;
    }
    const QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export unmatched paths"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
            QStringLiteral("/winamp-unmatched.txt"),
        QStringLiteral("Text files (*.txt);;All files (*)"));
    if (file.isEmpty()) {
        return;
    }
    std::ofstream out(std::filesystem::path(file.toStdString()), std::ios::trunc);
    if (!out) {
        setStatus(QStringLiteral("Could not write that file."));
        return;
    }
    for (const std::string& path : m_unmatched) {
        out << path << '\n';
    }
    setStatus(QStringLiteral("Exported %1 paths.").arg(m_unmatched.size()));
}

void MigrationDialog::setStatus(const QString& text) {
    m_status->setText(text);
}

} // namespace koalaamp::ui
