// SPDX-License-Identifier: MIT
// LibraryWindow — the local media library (LI-3). See LibraryWindow.hpp.

#include "ui/LibraryWindow.hpp"

#include "library/Query.hpp"
#include "library/Scanner.hpp"
#include "skins/Skin.hpp"
#include "ui/GenRenderer.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <utility>

namespace koalaamp::ui {
namespace {

int trackIndexRole() {
    return Qt::UserRole + 10;
}
int sortRole() {
    return Qt::UserRole + 20;
}

QString formatDuration(std::int64_t ms) {
    if (ms <= 0) {
        return QStringLiteral("--:--");
    }
    const auto total = ms / 1000;
    return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}

QString formatDate(std::int64_t unixSeconds) {
    if (unixSeconds <= 0) {
        return QStringLiteral("—");
    }
    return QDateTime::fromSecsSinceEpoch(unixSeconds).toString(QStringLiteral("yyyy-MM-dd"));
}

QString stars(int rating) {
    QString text;
    for (int i = 0; i < 5; ++i) {
        text += i < rating ? QChar(0x2605) : QChar(0x2606);
    }
    return text;
}

std::string slashed(std::string text) {
    for (char& c : text) {
        if (c == '\\' || c == '"') {
            c = ' '; // the grammar does not support quoted-string escapes
        }
    }
    return text;
}

class NumericItem final : public QTableWidgetItem {
public:
    NumericItem(const QString& text, qlonglong value) : QTableWidgetItem(text) {
        setData(sortRole(), value);
    }
    bool operator<(const QTableWidgetItem& other) const override {
        return data(sortRole()).toLongLong() < other.data(sortRole()).toLongLong();
    }
};

QColor colorAt(const skins::Skin& skin, int index) {
    const QImage* sheet = skin.sheet("GENEX");
    if (sheet == nullptr || sheet->isNull()) {
        return {};
    }
    const int x = 48 + index * 2;
    if (x >= sheet->width() || sheet->height() < 1) {
        return {};
    }
    return QColor::fromRgb(sheet->pixel(x, 0));
}

QString colorCss(const QColor& color, const char* fallback) {
    return color.isValid() ? color.name() : QString::fromLatin1(fallback);
}

} // namespace

LibraryWindow::LibraryWindow(std::string dbPath, PlayHandler play, QWidget* parent)
    : SkinnedWindow(parent), m_dbPath(std::move(dbPath)), m_play(std::move(play)), m_db(m_dbPath) {
    m_contentSize = genSnapSize(QSize(900, 560));
    setFixedSize(m_contentSize);
    setWindowTitle(QStringLiteral("KoalaAmp Media Library"));
    m_db.open();
    buildLayout();
    applySkinColors();
    if (!m_db.ready()) {
        setStatus(QStringLiteral("Library unavailable: %1")
                      .arg(QString::fromStdString(m_db.lastError())));
    } else {
        refreshAll();
    }
}

LibraryWindow::~LibraryWindow() {
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
}

void LibraryWindow::buildLayout() {
    const GenLayout layout = genLayout(m_contentSize);
    const QRect client = layout.client;
    const int margin = 7;
    const int x = client.x() + margin;
    const int width = client.width() - margin * 2;
    const int topY = client.y() + 6;
    const int rowHeight = 24;
    const int gap = 6;
    const int buttonWidth = 82;
    const int saveWidth = 92;

    m_views = new QComboBox(this);
    m_views->setObjectName(QStringLiteral("libraryViews"));
    m_views->setGeometry(x, topY, 170, rowHeight);

    const int rightButtons = buttonWidth * 2 + saveWidth + gap * 2;
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("librarySearch"));
    m_search->setPlaceholderText(QStringLiteral("Search, or ?artist has \"...\""));
    m_search->setGeometry(x + 176, topY, width - 176 - rightButtons, rowHeight);

    m_scan = new QPushButton(QStringLiteral("Scan…"), this);
    m_scan->setObjectName(QStringLiteral("libraryScan"));
    m_scan->setGeometry(x + width - rightButtons, topY, buttonWidth, rowHeight);

    m_refresh = new QPushButton(QStringLiteral("Refresh"), this);
    m_refresh->setObjectName(QStringLiteral("libraryRefresh"));
    m_refresh->setGeometry(x + width - rightButtons + buttonWidth + gap, topY, buttonWidth,
                           rowHeight);

    m_saveView = new QPushButton(QStringLiteral("Save view…"), this);
    m_saveView->setObjectName(QStringLiteral("librarySaveView"));
    m_saveView->setGeometry(x + width - saveWidth, topY, saveWidth, rowHeight);

    const int filterY = topY + rowHeight + gap;
    m_genre = new QComboBox(this);
    m_genre->setObjectName(QStringLiteral("libraryGenre"));
    m_genre->setGeometry(x, filterY, 180, rowHeight);

    m_year = new QComboBox(this);
    m_year->setObjectName(QStringLiteral("libraryYear"));
    m_year->setGeometry(x + 186, filterY, 100, rowHeight);

    const int contentY = filterY + rowHeight + gap;
    const int statusHeight = 18;
    const int contentHeight = client.bottom() - contentY - statusHeight - 4;
    const int treeWidth = 230;

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("libraryTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setAnimated(false);
    m_tree->setGeometry(x, contentY, treeWidth, contentHeight);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("libraryTracks"));
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels(
        QStringList{QStringLiteral("Title"), QStringLiteral("Time"), QStringLiteral("Artist"),
                    QStringLiteral("Album"), QStringLiteral("Rating"), QStringLiteral("Plays"),
                    QStringLiteral("Last played")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 190);
    m_table->setColumnWidth(1, 45);
    m_table->setColumnWidth(2, 105);
    m_table->setColumnWidth(3, 105);
    m_table->setColumnWidth(4, 64);
    m_table->setColumnWidth(5, 45);
    m_table->setGeometry(x + treeWidth + gap, contentY, width - treeWidth - gap, contentHeight);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("libraryStatus"));
    m_status->setGeometry(x, client.bottom() - statusHeight, width, statusHeight);

    QObject::connect(m_scan, &QPushButton::clicked, this, &LibraryWindow::scanFolder);
    QObject::connect(m_refresh, &QPushButton::clicked, this, &LibraryWindow::refreshAll);
    QObject::connect(m_search, &QLineEdit::returnPressed, this, &LibraryWindow::reloadTracks);
    QObject::connect(m_views, &QComboBox::currentIndexChanged, this,
                     [this](int) { reloadTracks(); });
    QObject::connect(m_genre, &QComboBox::currentIndexChanged, this,
                     [this](int) { reloadTracks(); });
    QObject::connect(m_year, &QComboBox::currentIndexChanged, this,
                     [this](int) { reloadTracks(); });
    QObject::connect(m_tree, &QTreeWidget::currentItemChanged, this,
                     [this](QTreeWidgetItem* item, QTreeWidgetItem*) { selectNode(item); });
    QObject::connect(m_tree, &QTreeWidget::itemExpanded, this,
                     [this](QTreeWidgetItem* item) { expandArtist(item); });
    QObject::connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
                     [this](QTreeWidgetItem* item, int) { selectNode(item); });
    QObject::connect(m_table, &QTableWidget::cellDoubleClicked, this,
                     [this](int row, int) { playRow(row); });
    QObject::connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column == 4) {
            rateRow(row);
        }
    });
    QObject::connect(m_saveView, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, QStringLiteral("Smart view"), QStringLiteral("View name:"),
                                  QLineEdit::Normal, QString(), &ok);
        if (!ok || name.trimmed().isEmpty()) {
            return;
        }
        const std::string query = m_search->text().trimmed().isEmpty()
                                      ? baseWhere() + filterSuffix()
                                      : m_search->text().toStdString();
        QSettings settings;
        settings.setValue(QStringLiteral("library/views/") + name.trimmed(),
                          QString::fromStdString(query));
        reloadViews();
        setStatus(QStringLiteral("Saved view %1").arg(name.trimmed()));
    });
}

void LibraryWindow::applySkinColors() {
    const skins::Skin* skin = m_skin != nullptr ? &m_skin->sheets : nullptr;
    QColor base(0x0e, 0x12, 0x16);
    QColor text(0xe8, 0xe8, 0xe8);
    QColor selected(0x1e, 0x4f, 0x68);
    QColor selectedText(0xff, 0xff, 0xff);
    QColor border(0x50, 0x50, 0x50);
    if (skin != nullptr) {
        const QColor candidateBase = colorAt(*skin, 0);
        const QColor candidateSelected = colorAt(*skin, 1);
        const QColor candidateText = colorAt(*skin, 2);
        const QColor candidateSelectedText = colorAt(*skin, 3);
        if (candidateBase.isValid()) {
            base = candidateBase;
        }
        if (candidateSelected.isValid()) {
            selected = candidateSelected;
        }
        if (candidateText.isValid()) {
            text = candidateText;
        }
        if (candidateSelectedText.isValid()) {
            selectedText = candidateSelectedText;
        }
        const QColor candidateBorder = colorAt(*skin, 8);
        if (candidateBorder.isValid()) {
            border = candidateBorder;
        }
    }
    // A dark base with dark text (or vice versa) is unreadable; fall back to the
    // designed high-contrast palette rather than trusting a malformed swatch.
    const int luminance = (base.red() + base.green() + base.blue()) / 3;
    if ((luminance > 128 && text.lightness() > 160) ||
        (luminance <= 128 && text.lightness() < 90)) {
        base = QColor(0x0e, 0x12, 0x16);
        text = QColor(0xe8, 0xe8, 0xe8);
    }
    setStyleSheet(
        QStringLiteral(
            "QLabel, QPushButton { color: %1; background: transparent; }"
            "QPushButton { border: 1px solid %2; padding: 2px 6px; }"
            "QLineEdit, QComboBox { color: %1; background: %3; border: 1px solid %2; }"
            "QTreeWidget, QTableWidget { color: %1; background: %3; border: 1px solid %2; "
            "selection-background-color: %4; selection-color: %5; }"
            "QHeaderView::section { color: %1; background: %3; border: 0; "
            "border-bottom: 1px solid %2; padding: 2px; }"
            "QTableWidget { gridline-color: %2; }"
            "QScrollBar:vertical { background: %3; width: 12px; }"
            "QScrollBar::handle:vertical { background: %4; min-height: 20px; }")
            .arg(colorCss(text, "#e8e8e8"), colorCss(border, "#505050"), colorCss(base, "#0e1216"),
                 colorCss(selected, "#1e4f68"), colorCss(selectedText, "#ffffff")));
    update();
}

void LibraryWindow::setSkin(const skins::SkinSet* skin) {
    m_skin = skin;
    setWindowShape(skin != nullptr ? skin->region : skins::Region{}, false, m_contentSize, 1);
    applySkinColors();
}

void LibraryWindow::paintEvent(QPaintEvent*) {
    const skins::Skin& sheets = m_skin != nullptr ? m_skin->sheets : skins::Skin{};
    const QImage chrome = renderGenWindow(sheets, nullptr, m_contentSize, "MEDIA LIBRARY",
                                          isActiveWindow(), 1, m_closePressed);
    QPainter painter(this);
    painter.drawImage(0, 0, chrome);
}

bool LibraryWindow::closeHit(const QPoint& point) const {
    return genLayout(m_contentSize).closeButton.contains(point);
}

bool LibraryWindow::isTitleBarAt(const QPoint& point) const {
    return genLayout(m_contentSize).title.contains(point) && !closeHit(point);
}

void LibraryWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && closeHit(event->pos())) {
        m_closePressed = true;
        update();
        event->accept();
        return;
    }
    SkinnedWindow::mousePressEvent(event);
}

void LibraryWindow::mouseReleaseEvent(QMouseEvent* event) {
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

void LibraryWindow::setStatus(const QString& text) {
    m_status->setText(text);
}

void LibraryWindow::reloadViews() {
    const QSignalBlocker blocker(m_views);
    m_views->clear();
    const auto add = [this](const QString& name, const QString& query, const QString& order) {
        const int index = m_views->count();
        m_views->addItem(name);
        m_views->setItemData(index, query, Qt::UserRole);
        m_views->setItemData(index, order, Qt::UserRole + 1);
    };
    add(QStringLiteral("All tracks"), QString(), QString());
    add(QStringLiteral("Recently added"), QStringLiteral("dateadded > [2 weeks ago]"),
        QStringLiteral("recent"));
    add(QStringLiteral("Recently played"), QStringLiteral("lastplay > [2 weeks ago]"),
        QStringLiteral("played"));
    add(QStringLiteral("Most played"), QStringLiteral("playcount > 0"), QStringLiteral("played"));
    add(QStringLiteral("Never played"), QStringLiteral("playcount = 0"), QStringLiteral("title"));
    add(QStringLiteral("Top rated"), QStringLiteral("rating > 0"), QStringLiteral("rating"));

    QSettings settings;
    settings.beginGroup(QStringLiteral("library/views"));
    for (const QString& name : settings.childKeys()) {
        add(name, settings.value(name).toString(), QString());
    }
    settings.endGroup();
    m_views->setCurrentIndex(0);
}

void LibraryWindow::refreshAll() {
    reloadViews();
    reloadFilters();
    m_search->clear();
    reloadArtists();
    reloadTracks();
}

void LibraryWindow::reloadFilters() {
    const QString genre = m_genre->currentData().toString();
    const QString year = m_year->currentData().toString();
    {
        const QSignalBlocker blocker(m_genre);
        m_genre->clear();
        m_genre->addItem(QStringLiteral("All genres"), QString());
        for (const std::string& value : m_db.genres()) {
            m_genre->addItem(QString::fromStdString(value), QString::fromStdString(value));
        }
        const int index = m_genre->findData(genre);
        m_genre->setCurrentIndex(index >= 0 ? index : 0);
    }
    {
        const QSignalBlocker blocker(m_year);
        m_year->clear();
        m_year->addItem(QStringLiteral("All years"), QString());
        for (int value : m_db.years()) {
            m_year->addItem(QString::number(value), QString::number(value));
        }
        const int index = m_year->findData(year);
        m_year->setCurrentIndex(index >= 0 ? index : 0);
    }
}

void LibraryWindow::reloadArtists() {
    m_loadingTree = true;
    m_tree->clear();
    auto* all = new QTreeWidgetItem(m_tree);
    all->setText(0, QStringLiteral("All tracks"));
    all->setData(0, Qt::UserRole, static_cast<int>(NodeKind::All));
    for (const library::ArtistRow& artist : m_db.artists()) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, QString::fromStdString(artist.name));
        item->setData(0, Qt::UserRole, static_cast<int>(NodeKind::Artist));
        item->setData(0, Qt::UserRole + 1, QString::fromStdString(artist.name));
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }
    m_loadingTree = false;
    m_tree->setCurrentItem(all);
}

void LibraryWindow::expandArtist(QTreeWidgetItem* item) {
    if (item == nullptr || item->childCount() > 0 ||
        static_cast<NodeKind>(item->data(0, Qt::UserRole).toInt()) != NodeKind::Artist) {
        return;
    }
    const std::string artist = item->data(0, Qt::UserRole + 1).toString().toStdString();
    for (const library::AlbumRow& album : m_db.albumsForArtist(artist)) {
        auto* child = new QTreeWidgetItem(item);
        child->setText(
            0, QStringLiteral("%1 (%2)")
                   .arg(QString::fromStdString(album.name))
                   .arg(album.year > 0 ? QString::number(album.year) : QStringLiteral("—")));
        child->setData(0, Qt::UserRole, static_cast<int>(NodeKind::Album));
        child->setData(0, Qt::UserRole + 1, QString::fromStdString(album.name));
        child->setData(0, Qt::UserRole + 2, QString::fromStdString(album.albumArtist));
    }
}

std::string LibraryWindow::baseWhere() const {
    const QString chosen = m_views->currentData(Qt::UserRole).toString();
    if (!chosen.isEmpty()) {
        return chosen.toStdString();
    }
    const QTreeWidgetItem* item = m_tree->currentItem();
    if (item == nullptr) {
        return "1";
    }
    const auto kind = static_cast<NodeKind>(item->data(0, Qt::UserRole).toInt());
    if (kind == NodeKind::Artist) {
        const std::string artist = item->data(0, Qt::UserRole + 1).toString().toStdString();
        return "(artist = \"" + slashed(artist) + "\" || album_artist = \"" + slashed(artist) +
               "\")";
    }
    if (kind == NodeKind::Album) {
        const std::string album = item->data(0, Qt::UserRole + 1).toString().toStdString();
        const std::string artist = item->data(0, Qt::UserRole + 2).toString().toStdString();
        return "(album = \"" + slashed(album) + "\" && album_artist = \"" + slashed(artist) + "\")";
    }
    return "1";
}

std::string LibraryWindow::filterSuffix() const {
    std::string suffix;
    if (!m_genre->currentData().toString().isEmpty()) {
        suffix +=
            " && genre = \"" + slashed(m_genre->currentData().toString().toStdString()) + "\"";
    }
    if (!m_year->currentData().toString().isEmpty()) {
        suffix += " && year = " + m_year->currentData().toString().toStdString();
    }
    return suffix;
}

bool LibraryWindow::parseOrReport(const std::string& text, library::ParsedQuery& out) {
    if (text.empty() || text == "1") {
        out.where = "1";
        out.params.clear();
        return true;
    }
    const library::QueryParseResult parsed = library::parseQuery(text);
    if (!parsed.query) {
        setStatus(QStringLiteral("Query error at %1: %2")
                      .arg(parsed.errorPos)
                      .arg(QString::fromStdString(parsed.error)));
        return false;
    }
    out = *parsed.query;
    return true;
}

void LibraryWindow::reloadTracks() {
    if (!m_db.ready()) {
        return;
    }
    std::string search = m_search->text().trimmed().toStdString();
    bool grammar = false;
    if (search.rfind("query:", 0) == 0) {
        search = search.substr(6);
        grammar = true;
    } else if (!search.empty() && search.front() == '?') {
        search = search.substr(1);
        grammar = true;
    }
    if (grammar) {
        library::ParsedQuery parsed;
        if (!parseOrReport(search, parsed)) {
            fillTable({}, false);
            return;
        }
        fillTable(m_db.query(parsed, 5000,
                             m_views->currentData(Qt::UserRole + 1).toString().toStdString()));
        return;
    }
    if (!search.empty()) {
        // Winamp's plain search box is a quick FTS lookup; grid filters only
        // apply to the browse/smart-view path.
        fillTable(m_db.search(search, 5000));
        return;
    }
    std::string where = baseWhere();
    where += filterSuffix();
    library::ParsedQuery parsed;
    if (!parseOrReport(where, parsed)) {
        fillTable({}, false);
        return;
    }
    fillTable(
        m_db.query(parsed, 5000, m_views->currentData(Qt::UserRole + 1).toString().toStdString()));
}

void LibraryWindow::selectNode(QTreeWidgetItem* item) {
    if (m_loadingTree || item == nullptr) {
        return;
    }
    // Browsing the tree takes over from a smart view; otherwise the smart-view
    // query would keep masking the artist/album the user just clicked.
    if (!m_views->currentData(Qt::UserRole).toString().isEmpty()) {
        const QSignalBlocker blocker(m_views);
        m_views->setCurrentIndex(0);
    }
    reloadTracks();
}

void LibraryWindow::fillTable(const std::vector<library::Track>& tracks, bool updateStatus) {
    m_tracks = tracks;
    m_table->setSortingEnabled(false);
    m_table->clearContents();
    m_table->setRowCount(static_cast<int>(tracks.size()));
    for (std::size_t row = 0; row < tracks.size(); ++row) {
        const library::Track& track = tracks[row];
        auto* title = new QTableWidgetItem(QString::fromStdString(track.title));
        auto* time = new NumericItem(formatDuration(track.durationMs), track.durationMs);
        auto* artist = new QTableWidgetItem(QString::fromStdString(track.artist));
        auto* album = new QTableWidgetItem(QString::fromStdString(track.album));
        auto* rating = new NumericItem(stars(track.rating), track.rating);
        auto* plays = new NumericItem(QString::number(track.playCount), track.playCount);
        auto* last = new QTableWidgetItem(formatDate(track.lastPlayed));
        QTableWidgetItem* const cells[] = {title, time, artist, album, rating, plays, last};
        for (QTableWidgetItem* cell : cells) {
            cell->setData(trackIndexRole(), static_cast<int>(row));
        }
        const int r = static_cast<int>(row);
        m_table->setItem(r, 0, title);
        m_table->setItem(r, 1, time);
        m_table->setItem(r, 2, artist);
        m_table->setItem(r, 3, album);
        m_table->setItem(r, 4, rating);
        m_table->setItem(r, 5, plays);
        m_table->setItem(r, 6, last);
    }
    m_table->setSortingEnabled(true);
    if (updateStatus) {
        setStatus(QStringLiteral("%1 track(s)").arg(tracks.size()));
    }
}

void LibraryWindow::rateRow(int row) {
    QTableWidgetItem* cell = m_table->item(row, 0);
    if (cell == nullptr) {
        return;
    }
    const int index = cell->data(trackIndexRole()).toInt();
    if (index < 0 || static_cast<std::size_t>(index) >= m_tracks.size()) {
        return;
    }
    library::Track& track = m_tracks[static_cast<std::size_t>(index)];
    track.rating = (track.rating + 1) % 6;
    if (m_db.setRating(track.path, track.rating)) {
        QTableWidgetItem* rating = m_table->item(row, 4);
        if (rating != nullptr) {
            rating->setText(stars(track.rating));
        }
        setStatus(QStringLiteral("Rated %1: %2 stars")
                      .arg(QString::fromStdString(track.title))
                      .arg(track.rating));
    } else {
        setStatus(QStringLiteral("Could not write rating"));
    }
}

void LibraryWindow::playRow(int row) {
    QTableWidgetItem* cell = m_table->item(row, 0);
    if (cell == nullptr || m_table->rowCount() <= 0) {
        return;
    }
    // Follow the order the user sees, not the DB's storage order: the grid is
    // sortable, so rebuild the visible sequence and locate the clicked row in it.
    std::vector<library::Track> ordered;
    ordered.reserve(static_cast<std::size_t>(m_table->rowCount()));
    std::size_t start = 0;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const QTableWidgetItem* item = m_table->item(r, 0);
        if (item == nullptr) {
            continue;
        }
        const int index = item->data(trackIndexRole()).toInt();
        if (index < 0 || static_cast<std::size_t>(index) >= m_tracks.size()) {
            continue;
        }
        if (r == row) {
            start = ordered.size();
        }
        ordered.push_back(m_tracks[static_cast<std::size_t>(index)]);
    }
    if (!ordered.empty() && m_play) {
        m_play(ordered, start);
    }
}

void LibraryWindow::scanFolder() {
    if (m_scanning.load()) {
        return;
    }
    const QString start = QSettings().value(QStringLiteral("library/scanRoot")).toString();
    QFileDialog dialog(this, QStringLiteral("Scan music folder"), start);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    // The skinned windows are XCB/XWayland; the platform-native picker can be a
    // portal dialog that never appears in a bare window-manager session.
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return;
    }
    const QString folder = dialog.selectedFiles().front();
    QSettings().setValue(QStringLiteral("library/scanRoot"), folder);
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
    m_scanning.store(true);
    m_scan->setEnabled(false);
    setStatus(QStringLiteral("Scanning %1…").arg(folder));
    m_scanThread = std::thread([this, folder] {
        library::LibraryDb db(m_dbPath);
        if (!db.open()) {
            QMetaObject::invokeMethod(
                this,
                [this, error = db.lastError()] {
                    m_scanning.store(false);
                    m_scan->setEnabled(true);
                    setStatus(QStringLiteral("Scan failed: %1").arg(QString::fromStdString(error)));
                },
                Qt::QueuedConnection);
            return;
        }
        db.begin();
        library::ScanCallbacks callbacks;
        callbacks.onTrack = [&db](const library::ScannedTrack& track) { db.upsert(track); };
        callbacks.known = [&db](const std::string& path) { return db.stamp(path); };
        const library::ScanSummary summary =
            library::scanRoots({std::filesystem::path(folder.toStdString())}, callbacks);
        db.commit();
        QMetaObject::invokeMethod(
            this,
            [this, summary] {
                m_scanning.store(false);
                m_scan->setEnabled(true);
                refreshAll();
                setStatus(QStringLiteral("Scan: %1 added, %2 updated, %3 unchanged")
                              .arg(summary.added)
                              .arg(summary.updated)
                              .arg(summary.unchanged));
            },
            Qt::QueuedConnection);
    });
}

} // namespace koalaamp::ui
