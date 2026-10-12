// SPDX-License-Identifier: MIT
#include "ui/PlaylistWindow.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QUrl>
#include <QWheelEvent>

#include "playlist/Playlist.hpp"
#include "ui/Atf.hpp"
#include "ui/JumpToFileDialog.hpp"
#include "ui/PleditRenderer.hpp"

namespace koalaamp::ui {

namespace {

/// Playlist scrolls in whole track jumps (Webamp `scrollUpFourTracks`).
constexpr int kScrollRows = 4;
constexpr int kTickMs = 250;

const std::vector<std::string>& checklistStorage() {
    static const std::vector<std::string> items = {
        "row-click-selects-single",
        "ctrl-click-toggles-selection",
        "shift-click-selects-range",
        "ctrl-a-selects-all",
        "row-double-click-plays",
        "row-drag-reorders-rows",
        "row-drag-reorders-queue",
        "delete-removes-selected",
        "wheel-scrolls-list",
        "scrollbar-drag-scrolls",
        "scroll-down-button-scrolls",
        "J-opens-jump-dialog",
        "jump-dialog-filters-fuzzy",
        "jump-select-jumps",
        "jump-enqueue-adds-to-queue",
        "jump-clear-empties-queue",
        "context-menu-opens-with-verbs",
        "context-play-plays-row",
        "context-enqueue-queues-row",
        "context-remove-deletes-row",
        "shade-button-collapses",
        "close-button-emits-close",
        "resize-grip-changes-segments",
        "action-play-drives-transport",
        "action-stop-drives-transport",
        "action-next-drives-transport",
        "action-eject-requests-files",
        "load-into-queue-enqueues-all",
        "enqueue-highlight-shows-position",
        "queue-consumed-updates-positions",
        "title-bar-shows-selected-art",
        "shade-bar-renders-14px",
        "segments-tiling-resizes-window",
    };
    return items;
}

playback::MediaRef mediaFor(const PleditEntry& entry) {
    return playback::MediaRef{playback::MediaRef::Kind::Path, entry.filename};
}

/// PL-1 default autosave debounce: long enough not to thrash the disk on a
/// drag-reorder, short enough that a crash loses at most a few seconds.
constexpr int kAutosaveDebounceMs = 5000;

playlist::Track toTrack(const PleditEntry& entry) {
    return playlist::Track{entry.title, entry.filename, entry.lengthMs};
}

PleditEntry toEntry(const playlist::Track& track) {
    return PleditEntry{track.title, track.filename, track.lengthMs};
}

/// True when `path` names a playlist format the open/drop verbs accept.
bool isPlaylistFile(const std::string& path) {
    return playlist::formatForExtension(std::filesystem::path(path).extension().string()) !=
           playlist::Format::Unknown;
}

}  // namespace

const std::vector<std::string>& playlistInteractionChecklist() { return checklistStorage(); }
const std::vector<std::string>& PlaylistWindow::interactionChecklist() {
    return checklistStorage();
}

PlaylistWindow::PlaylistWindow(QWidget* parent) : SkinnedWindow(parent) {
    m_titleFormat = std::string(kDefaultAtf);
    buildUi();
    setWindowTitle(QStringLiteral("Playlist Editor"));
    setAcceptDrops(true);
    updateShape();
    applySkinToCanvas();

    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &PlaylistWindow::tick);
    m_timer.start();

    m_autosaveTimer.setSingleShot(true);
    m_autosaveTimer.setInterval(kAutosaveDebounceMs);
    connect(&m_autosaveTimer, &QTimer::timeout, this, [this] { saveAutosaveNow(); });

    connect(this, &SkinnedWindow::shadeToggleRequested, this,
            [this] { setShade(!m_state.shade); });
}

PlaylistWindow::~PlaylistWindow() {
    closeMenu();
    // On quit the debounce may still be armed: flush it (PL-1 "on quit").
    if (m_autosaveEnabled && !m_autosaveDir.empty()) {
        saveAutosaveNow();
    }
}

void PlaylistWindow::buildUi() {
    m_canvas = new PleditCanvas(this);
    m_canvas->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_canvas->move(0, 0);
}

void PlaylistWindow::setSkin(const skins::SkinSet* skin) {
    m_skin = skin;
    applySkinToCanvas();
    updateShape();
    requestRepaint();
}

void PlaylistWindow::applySkinToCanvas() {
    static const skins::PleditColors fallback = skins::defaultPleditColors();
    if (m_skin != nullptr) {
        m_canvas->setSkin(&m_skin->sheets, m_skin->pleditColors);
    } else {
        m_canvas->setSkin(nullptr, fallback);
    }
}

void PlaylistWindow::setTransport(ITransport* transport) {
    m_transport = transport;
    syncFromTransport();
}

void PlaylistWindow::setState(const PleditState& state) {
    m_state = state;
    clampScroll(m_state);
    updateShape();
    requestRepaint();
}

void PlaylistWindow::setEntries(std::vector<PleditEntry> entries) {
    m_state.entries = std::move(entries);
    clearSelection(m_state);
    koalaamp::ui::clearQueue(m_state);
    m_state.currentIndex = -1;
    m_state.scrollOffset = 0;
    requestRepaint();
    touchAutosave();
}

void PlaylistWindow::addEntries(std::vector<PleditEntry> entries) {
    const int count = static_cast<int>(entries.size());
    appendEntries(m_state, std::move(entries));
    requestRepaint();
    emit entriesAdded(count);
    touchAutosave();
}

void PlaylistWindow::addMediaFiles(const std::vector<std::string>& paths) {
    std::vector<PleditEntry> entries;
    entries.reserve(paths.size());
    for (const std::string& path : paths) {
        const playback::MediaRef media{playback::MediaRef::Kind::Path, path};
        entries.push_back(PleditEntry{formatAtf(m_titleFormat, atfFieldsFor(media)), path, 0});
    }
    addEntries(std::move(entries));
}

// ---------------------------------------------------------------------------
// PL-1: playlist files + autosave
// ---------------------------------------------------------------------------

const std::vector<std::string>& PlaylistWindow::playlistExtensions() {
    return playlist::supportedExtensions();
}

bool PlaylistWindow::openPlaylist(const std::string& path) {
    const playlist::Playlist parsed = playlist::readFile(path);
    if (parsed.format == playlist::Format::Unknown) {
        return false;
    }
    std::vector<PleditEntry> entries;
    entries.reserve(parsed.tracks.size());
    for (const playlist::Track& track : parsed.tracks) {
        entries.push_back(toEntry(track));
    }
    setEntries(std::move(entries));
    return true;
}

bool PlaylistWindow::addPlaylist(const std::string& path) {
    const playlist::Playlist parsed = playlist::readFile(path);
    if (parsed.format == playlist::Format::Unknown) {
        return false;
    }
    std::vector<PleditEntry> entries;
    entries.reserve(parsed.tracks.size());
    for (const playlist::Track& track : parsed.tracks) {
        entries.push_back(toEntry(track));
    }
    addEntries(std::move(entries));
    return true;
}

bool PlaylistWindow::savePlaylist(const std::string& path) const {
    const playlist::Format format =
        playlist::formatForExtension(std::filesystem::path(path).extension().string());
    if (format == playlist::Format::Unknown) {
        return false;
    }
    std::vector<playlist::Track> tracks;
    tracks.reserve(m_state.entries.size());
    for (const PleditEntry& entry : m_state.entries) {
        tracks.push_back(toTrack(entry));
    }
    return playlist::writeFile(path, tracks, format);
}

void PlaylistWindow::setAutosaveDir(const std::string& dir) {
    m_autosaveDir = dir;
}

void PlaylistWindow::setAutosaveEnabled(bool on) {
    m_autosaveEnabled = on;
    if (!on) {
        m_autosaveTimer.stop();
        m_autosaveDirty = false;
    } else if (m_autosaveDirty) {
        m_autosaveTimer.start();
    }
}

void PlaylistWindow::setAutosaveDebounceMs(int ms) {
    m_autosaveTimer.setInterval(std::max(0, ms));
}

void PlaylistWindow::touchAutosave() {
    if (!m_autosaveEnabled || m_autosaveDir.empty()) {
        return;
    }
    m_autosaveDirty = true;
    m_autosaveTimer.start();
}

bool PlaylistWindow::saveAutosaveNow() {
    if (!m_autosaveEnabled || m_autosaveDir.empty()) {
        return false;
    }
    playlist::AutosaveState state;
    state.tracks.reserve(m_state.entries.size());
    for (const PleditEntry& entry : m_state.entries) {
        state.tracks.push_back(toTrack(entry));
    }
    state.queue = m_state.queue;
    state.currentIndex = m_state.currentIndex;
    const bool ok = playlist::saveAutosave(m_autosaveDir, state);
    m_autosaveDirty = false;
    return ok;
}

bool PlaylistWindow::restoreAutosave() {
    if (m_autosaveDir.empty()) {
        return false;
    }
    const auto state = playlist::loadAutosave(m_autosaveDir);
    if (!state.has_value()) {
        return false;
    }
    std::vector<PleditEntry> entries;
    entries.reserve(state->tracks.size());
    for (const playlist::Track& track : state->tracks) {
        entries.push_back(toEntry(track));
    }
    setEntries(std::move(entries));
    m_state.queue = state->queue;
    if (state->currentIndex >= 0 &&
        static_cast<std::size_t>(state->currentIndex) < m_state.entries.size()) {
        m_state.currentIndex = state->currentIndex;
    }
    clampScroll(m_state);
    requestRepaint();
    return true;
}

void PlaylistWindow::setTitleFormat(std::string format) {
    m_titleFormat = std::move(format);
    retitleEntries();
    requestRepaint();
}

void PlaylistWindow::retitleEntries() {
    for (auto& entry : m_state.entries) {
        const playback::MediaRef media{playback::MediaRef::Kind::Path, entry.filename};
        entry.title = formatAtf(m_titleFormat, atfFieldsFor(media));
    }
}

void PlaylistWindow::setScale(int scale) {
    const int clamped = std::clamp(scale, 1, 4);
    if (clamped == m_scale) {
        return;
    }
    m_scale = clamped;
    updateShape();
    requestRepaint();
}

void PlaylistWindow::setShade(bool shaded) {
    if (shaded == m_state.shade) {
        return;
    }
    m_state.shade = shaded;
    updateShape();
    requestRepaint();
    emit shadeChanged(shaded);
}

void PlaylistWindow::setAlwaysOnTop(bool on) {
    const QPoint position = pos();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    // Qt recreates the native window when a flag changes; restore the place.
    show();
    move(position);
    requestRepaint();
    emit alwaysOnTopChanged(on);
}

bool PlaylistWindow::isAlwaysOnTop() const {
    return (windowFlags() & Qt::WindowStaysOnTopHint) != 0;
}

void PlaylistWindow::setSegments(int widthSegments, int heightSegments) {
    m_state.widthSegments = std::max(0, widthSegments);
    m_state.heightSegments = std::max(0, heightSegments);
    clampScroll(m_state);
    updateShape();
    requestRepaint();
}

void PlaylistWindow::applyGeometry(const PleditGeometry& geometry) {
    applyPleditGeometry(m_state, geometry);
    updateShape();
    requestRepaint();
}

void PlaylistWindow::updateShape() {
    const int width = pleditWidth(m_state);
    const int height = m_state.shade ? kPleditShadeHeight : pleditHeight(m_state);
    clearMask();  // the playlist is always rectangular (region.txt shapes main/EQ)
    setFixedSize(width * m_scale, height * m_scale);
    m_canvas->setScale(m_scale);
    m_canvas->setFixedSize(width * m_scale, height * m_scale);
}

void PlaylistWindow::requestRepaint() {
    m_canvas->setState(m_state);
    update();
}

// ---------------------------------------------------------------------------
// Transport / model verbs
// ---------------------------------------------------------------------------

void PlaylistWindow::syncFromTransport() {
    const std::size_t queueBefore = m_state.queue.size();
    if (m_transport != nullptr) {
        const auto media = m_transport->currentMedia();
        if (media.has_value()) {
            const auto it = std::find_if(
                m_state.entries.begin(), m_state.entries.end(),
                [&](const PleditEntry& entry) { return entry.filename == media->value; });
            m_state.currentIndex = it == m_state.entries.end()
                                       ? -1
                                       : static_cast<int>(std::distance(m_state.entries.begin(), it));
        } else {
            m_state.currentIndex = -1;
        }

        // Reconcile the window's queue view with the engine's treatment of it:
        // every queued track the engine has already consumed drops off the
        // front (the queue is consumed before any shuffle refill).
        const std::size_t engineSize = m_transport->queueSize();
        if (engineSize == 0) {
            koalaamp::ui::clearQueue(m_state);
        } else {
            while (m_state.queue.size() > engineSize) {
                popQueueFront(m_state);
            }
        }
    }
    clampScroll(m_state);
    requestRepaint();
    if (m_state.queue.size() != queueBefore) {
        touchAutosave();
    }
}

void PlaylistWindow::tick() { syncFromTransport(); }

void PlaylistWindow::playRow(std::size_t index) {
    if (index >= m_state.entries.size() || m_transport == nullptr) {
        return;
    }
    m_state.currentIndex = static_cast<int>(index);
    scrollToVisible(m_state, index);
    m_transport->playMedia(mediaFor(m_state.entries[index]));
    requestRepaint();
    emit trackActivated(static_cast<int>(index));
}

void PlaylistWindow::playSelectedOrFirst() {
    if (m_transport == nullptr) {
        return;
    }
    if (!m_state.selected.empty()) {
        playRow(m_state.selected.front());
    } else if (!m_state.entries.empty()) {
        playRow(0);
    } else {
        emit mediaOpenRequested();
    }
}

void PlaylistWindow::enqueueRow(std::size_t index) {
    if (index >= m_state.entries.size()) {
        return;
    }
    if (m_transport != nullptr) {
        m_transport->enqueue(mediaFor(m_state.entries[index]));
    }
    enqueueIndex(m_state, index);
    requestRepaint();
    touchAutosave();
}

void PlaylistWindow::enqueueSelected() {
    const std::vector<std::size_t> selection = m_state.selected;
    for (const std::size_t index : selection) {
        enqueueRow(index);
    }
}

void PlaylistWindow::loadIntoQueue() {
    clearQueue();
    for (std::size_t i = 0; i < m_state.entries.size(); ++i) {
        enqueueRow(i);
    }
}

void PlaylistWindow::clearQueue() {
    if (m_transport != nullptr) {
        m_transport->clearQueue();
    }
    koalaamp::ui::clearQueue(m_state);
    requestRepaint();
    touchAutosave();
}

void PlaylistWindow::removeSelected() {
    const std::size_t removed = koalaamp::ui::removeSelected(m_state);
    if (removed > 0) {
        requestRepaint();
        touchAutosave();
    }
}

void PlaylistWindow::removeRow(std::size_t index) {
    if (index >= m_state.entries.size()) {
        return;
    }
    selectSingle(m_state, index);
    removeSelected();
}

void PlaylistWindow::selectAllRows() {
    selectAll(m_state);
    requestRepaint();
}

void PlaylistWindow::scrollBy(int rows) {
    m_state.scrollOffset += rows;
    clampScroll(m_state);
    requestRepaint();
}

// ---------------------------------------------------------------------------
// Menus / dialogs
// ---------------------------------------------------------------------------

void PlaylistWindow::closeMenu() {
    if (m_menu != nullptr) {
        m_menu->hide();
        delete m_menu;
        m_menu = nullptr;
    }
}

void PlaylistWindow::openContextMenu(const QPoint& localPoint, int row) {
    closeMenu();
    m_menu = new QMenu(this);
    QMenu* menu = m_menu;

    if (row >= 0 && row < static_cast<int>(m_state.entries.size())) {
        const std::size_t index = static_cast<std::size_t>(row);
        menu->addAction(QStringLiteral("Play"), this, [this, index] { playRow(index); });
        menu->addAction(QStringLiteral("Enqueue"), this, [this, index] { enqueueRow(index); });
        menu->addAction(QStringLiteral("Remove"), this, [this, index] { removeRow(index); });
        menu->addSeparator();
        auto* rating = menu->addMenu(QStringLiteral("Rating"));
        rating->addAction(QStringLiteral("(stub)"))->setEnabled(false);
        menu->addAction(QStringLiteral("Jump to file..."), this,
                        [this] { openJumpDialog(); });
    } else {
        menu->addAction(QStringLiteral("Select all"), this,
                        [this] { selectAllRows(); });
        menu->addAction(QStringLiteral("Jump to file..."), this,
                        [this] { openJumpDialog(); });
    }
    menu->popup(mapToGlobal(localPoint));
}

void PlaylistWindow::openMenuFor(PleditHitId id, const QPoint& localPoint) {
    closeMenu();
    m_menu = new QMenu(this);
    QMenu* menu = m_menu;

    switch (id) {
        case PleditHitId::MenuAdd:
            menu->addAction(QStringLiteral("Add file(s)..."), this,
                            [this] { emit mediaOpenRequested(); });
            break;
        case PleditHitId::MenuRemove:
            menu->addAction(QStringLiteral("Remove selected"), this,
                            [this] { removeSelected(); });
            menu->addAction(QStringLiteral("Remove all"), this, [this] {
                setEntries({});
            });
            break;
        case PleditHitId::MenuSelection:
            menu->addAction(QStringLiteral("Select all"), this,
                            [this] { selectAllRows(); });
            menu->addAction(QStringLiteral("Select none"), this, [this] {
                clearSelection(m_state);
                requestRepaint();
            });
            break;
        case PleditHitId::MenuMisc:
            menu->addAction(QStringLiteral("Open playlist..."), this,
                            [this] { emit playlistOpenRequested(); });
            menu->addAction(QStringLiteral("Save playlist..."), this,
                            [this] { emit playlistSaveRequested(); });
            menu->addSeparator();
            menu->addAction(QStringLiteral("Jump to file..."), this,
                            [this] { openJumpDialog(); });
            menu->addAction(QStringLiteral("Load playlist into queue"), this,
                            [this] { loadIntoQueue(); });
            break;
        default:
            break;
    }
    menu->popup(mapToGlobal(localPoint));
}

void PlaylistWindow::openJumpDialog() {
    if (!m_jumpEnabled) {
        return;
    }
    if (m_jumpDialog == nullptr) {
        m_jumpDialog = std::make_unique<JumpToFileDialog>(m_state, this);
        connect(m_jumpDialog.get(), &JumpToFileDialog::jumpRequested, this,
                [this](int index) { playRow(static_cast<std::size_t>(index)); });
        connect(m_jumpDialog.get(), &JumpToFileDialog::enqueueRequested, this,
                [this](int index) { enqueueRow(static_cast<std::size_t>(index)); });
        connect(m_jumpDialog.get(), &JumpToFileDialog::clearQueueRequested, this,
                [this] { clearQueue(); });
    } else {
        m_jumpDialog->setState(m_state);
    }
    m_jumpDialog->show();
    m_jumpDialog->raise();
    m_jumpDialog->activateWindow();
}

bool PlaylistWindow::jumpDialogVisible() const {
    return m_jumpDialog != nullptr && m_jumpDialog->isVisible();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

PleditHitId PlaylistWindow::controlAt(const QPoint& point) const {
    return pleditHitTest(m_state, point, m_scale);
}

bool PlaylistWindow::isTitleBarAt(const QPoint& point) const {
    return controlAt(point) == PleditHitId::TitleBar;
}

void PlaylistWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        const int row = pleditRowAt(m_state, event->pos(), m_scale);
        openContextMenu(event->pos(), row);
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const PleditHitId id = controlAt(event->pos());
    if (id == PleditHitId::TitleBar) {
        SkinnedWindow::mousePressEvent(event);  // start the window drag
        return;
    }
    const int row = pleditRowAt(m_state, event->pos(), m_scale);
    if (row >= 0) {
        pressRow(row, event->modifiers());
        event->accept();
        return;
    }
    pressControl(id, event->pos());
    event->accept();
}

void PlaylistWindow::mouseMoveEvent(QMouseEvent* event) {
    if (isDragging()) {
        SkinnedWindow::mouseMoveEvent(event);
        return;
    }
    if (m_drag != Drag::None) {
        dragControl(event->pos());
        event->accept();
        return;
    }
    updateHover(event->pos());
    QWidget::mouseMoveEvent(event);
}

void PlaylistWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        SkinnedWindow::mouseReleaseEvent(event);
        return;
    }
    if (isDragging()) {
        SkinnedWindow::mouseReleaseEvent(event);
        return;
    }
    if (m_drag == Drag::Rows) {
        finishRowDrag();
        event->accept();
        return;
    }
    const PleditHitId pressed = m_pressed;
    m_pressed = PleditHitId::None;
    m_drag = Drag::None;
    if (pressed != PleditHitId::None) {
        const bool same = controlAt(event->pos()) == pressed;
        releaseControl(pressed, event->pos());
        if (same) {
            activate(pressed);
        }
    }
    event->accept();
}

void PlaylistWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (isTitleBarAt(event->pos())) {
            SkinnedWindow::mouseDoubleClickEvent(event);
            return;
        }
        const int row = pleditRowAt(m_state, event->pos(), m_scale);
        if (row >= 0) {
            playRow(static_cast<std::size_t>(row));
            event->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void PlaylistWindow::wheelEvent(QWheelEvent* event) {
    const int steps = event->angleDelta().y() / 120;
    if (steps != 0) {
        scrollBy(-steps * 3);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void PlaylistWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_J:
            openJumpDialog();
            break;
        case Qt::Key_A:
            if ((event->modifiers() & Qt::ControlModifier) != 0) {
                selectAllRows();
            } else {
                QWidget::keyPressEvent(event);
                return;
            }
            break;
        case Qt::Key_Delete:
            removeSelected();
            break;
        case Qt::Key_Up: {
            const int row = m_state.selected.empty()
                                ? 0
                                : std::max(0, static_cast<int>(m_state.selected.front()) - 1);
            selectSingle(m_state, static_cast<std::size_t>(row));
            scrollToVisible(m_state, static_cast<std::size_t>(row));
            m_anchor = row;
            requestRepaint();
            break;
        }
        case Qt::Key_Down: {
            const int last = std::max(0, static_cast<int>(m_state.entries.size()) - 1);
            const int row = m_state.selected.empty()
                                ? 0
                                : std::min(last, static_cast<int>(m_state.selected.front()) + 1);
            selectSingle(m_state, static_cast<std::size_t>(row));
            scrollToVisible(m_state, static_cast<std::size_t>(row));
            m_anchor = row;
            requestRepaint();
            break;
        }
        case Qt::Key_Home:
            m_state.scrollOffset = 0;
            clampScroll(m_state);
            requestRepaint();
            break;
        case Qt::Key_End:
            m_state.scrollOffset = static_cast<int>(m_state.entries.size());
            clampScroll(m_state);
            requestRepaint();
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            playSelectedOrFirst();
            break;
        case Qt::Key_Escape:
            closeMenu();
            break;
        default:
            QWidget::keyPressEvent(event);
            return;
    }
    event->accept();
}

void PlaylistWindow::leaveEvent(QEvent* event) {
    if (m_state.hoverIndex != -1) {
        m_state.hoverIndex = -1;
        requestRepaint();
    }
    QWidget::leaveEvent(event);
}

// --- drag & drop (PL-1) -----------------------------------------------------
// Winamp accepts playlists and media files dropped on the playlist editor;
// a dropped .m3u/.pls/… is *added* (the "add" semantics), media files are
// appended as new rows.

void PlaylistWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    QWidget::dragEnterEvent(event);
}

void PlaylistWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QWidget::dragMoveEvent(event);
}

void PlaylistWindow::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        QWidget::dropEvent(event);
        return;
    }
    std::vector<std::string> playlistPaths;
    std::vector<std::string> mediaPaths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const std::string path = url.toLocalFile().toStdString();
        if (isPlaylistFile(path)) {
            playlistPaths.push_back(path);
        } else {
            mediaPaths.push_back(path);
        }
    }
    for (const std::string& path : playlistPaths) {
        addPlaylist(path);
    }
    if (!mediaPaths.empty()) {
        addMediaFiles(mediaPaths);
    }
    if (!playlistPaths.empty() || !mediaPaths.empty()) {
        event->acceptProposedAction();
    }
}

void PlaylistWindow::pressRow(int row, Qt::KeyboardModifiers modifiers) {
    const std::size_t index = static_cast<std::size_t>(row);
    if ((modifiers & Qt::ControlModifier) != 0) {
        selectToggle(m_state, index);
        m_anchor = row;
    } else if ((modifiers & Qt::ShiftModifier) != 0) {
        selectRange(m_state, static_cast<std::size_t>(std::max(0, m_anchor)), index);
    } else if (!isSelected(m_state, index)) {
        selectSingle(m_state, index);
        m_anchor = row;
    } else {
        m_anchor = row;
    }
    beginRowDrag(row);
    requestRepaint();
}

void PlaylistWindow::beginRowDrag(int row) {
    m_drag = Drag::Rows;
    m_dragRow = row;
    m_dropRow = row;
    m_rowDragActive = false;
}

void PlaylistWindow::finishRowDrag() {
    if (m_rowDragActive && m_dropRow >= 0) {
        moveSelected(m_state, static_cast<std::size_t>(m_dropRow));
    }
    m_drag = Drag::None;
    m_rowDragActive = false;
    m_dragRow = -1;
    m_dropRow = -1;
    m_state.hoverIndex = -1;
    requestRepaint();
}

void PlaylistWindow::updateHover(const QPoint& point) {
    const int row = pleditRowAt(m_state, point, m_scale);
    if (row != m_state.hoverIndex) {
        m_state.hoverIndex = row;
        requestRepaint();
    }
}

void PlaylistWindow::pressControl(PleditHitId id, const QPoint& point) {
    m_pressed = id;
    switch (id) {
        case PleditHitId::CloseButton:
            m_state.closePressed = true;
            break;
        case PleditHitId::ShadeButton:
            m_state.shadePressed = true;
            break;
        case PleditHitId::ResizeGrip:
            m_drag = Drag::Resize;
            m_pressGlobal = mapToGlobal(point);
            m_resizeStartW = m_state.widthSegments;
            m_resizeStartH = m_state.heightSegments;
            m_state.resizeActive = true;
            break;
        case PleditHitId::ScrollBar:
            m_drag = Drag::ScrollHandle;
            m_scrollStartGlobal = mapToGlobal(point);
            m_scrollStartOffset = m_state.scrollOffset;
            if (!pleditScrollHandle(m_state).contains(point)) {
                scrollFromPoint(point.y());
            }
            break;
        default:
            break;
    }
    requestRepaint();
}

void PlaylistWindow::dragControl(const QPoint& point) {
    const QPoint global = mapToGlobal(point);
    switch (m_drag) {
        case Drag::Resize: {
            const int dw = (global.x() - m_pressGlobal.x()) / kPleditSegmentWidth;
            const int dh = (global.y() - m_pressGlobal.y()) / kPleditSegmentHeight;
            setSegments(m_resizeStartW + dw, m_resizeStartH + dh);
            break;
        }
        case Drag::ScrollHandle:
            scrollFromPoint(point.y());
            break;
        case Drag::Rows: {
            const int row = pleditRowAt(m_state, point, m_scale);
            if (row >= 0 && row != m_dragRow) {
                m_rowDragActive = true;
                m_dropRow = row;
                m_state.hoverIndex = row;
                requestRepaint();
            }
            break;
        }
        default:
            break;
    }
    (void)global;
}

void PlaylistWindow::releaseControl(PleditHitId id, const QPoint& /*point*/) {
    if (id == PleditHitId::ResizeGrip) {
        m_state.resizeActive = false;
    }
    m_state.closePressed = false;
    m_state.shadePressed = false;
    requestRepaint();
}

void PlaylistWindow::activate(PleditHitId id) {
    switch (id) {
        case PleditHitId::CloseButton:
            emit closeRequested();
            break;
        case PleditHitId::ShadeButton:
            setShade(!m_state.shade);
            break;
        case PleditHitId::ActionPrevious:
            if (m_transport != nullptr) {
                m_transport->previous();
            }
            break;
        case PleditHitId::ActionPlay:
            playSelectedOrFirst();
            break;
        case PleditHitId::ActionPause:
            if (m_transport != nullptr) {
                m_transport->togglePause();
            }
            break;
        case PleditHitId::ActionStop:
            if (m_transport != nullptr) {
                m_transport->stop();
            }
            break;
        case PleditHitId::ActionNext:
            if (m_transport != nullptr) {
                m_transport->next();
            }
            break;
        case PleditHitId::ActionEject:
            emit mediaOpenRequested();
            break;
        case PleditHitId::ScrollUp:
            scrollBy(-kScrollRows);
            break;
        case PleditHitId::ScrollDown:
            scrollBy(kScrollRows);
            break;
        case PleditHitId::MenuAdd:
        case PleditHitId::MenuRemove:
        case PleditHitId::MenuSelection:
        case PleditHitId::MenuMisc: {
            const int menu = pleditMenuIndex(id);
            openMenuFor(id, pleditMenuButton(m_state, menu).bottomLeft());
            break;
        }
        default:
            break;
    }
    syncFromTransport();
}

void PlaylistWindow::scrollFromPoint(int localY) {
    const QRect bar = pleditScrollBar(m_state);
    const int visible = pleditVisibleTracks(m_state.heightSegments);
    const int overflow = std::max(0, static_cast<int>(m_state.entries.size()) - visible);
    if (overflow == 0) {
        m_state.scrollOffset = 0;
    } else {
        const int travel = std::max(1, bar.height() - 18);
        const double fraction =
            std::clamp(static_cast<double>(localY - bar.top()) / travel, 0.0, 1.0);
        m_state.scrollOffset = static_cast<int>(std::lround(fraction * overflow));
        clampScroll(m_state);
    }
    requestRepaint();
}

}  // namespace koalaamp::ui
