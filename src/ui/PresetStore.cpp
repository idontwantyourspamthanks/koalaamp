// SPDX-License-Identifier: MIT
//
// PresetStore — see PresetStore.hpp for the contract and the ticket reference.
#include "ui/PresetStore.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace koalaamp::ui {
namespace {

/// Absolute, lexically normalized form of `path` — no filesystem access, so this
/// works for a root that does not exist yet.
std::string absoluteNormalized(const std::string& path) {
    std::error_code ec;
    std::filesystem::path result = std::filesystem::absolute(path, ec);
    if (ec) {
        return path;
    }
    return result.lexically_normal().string();
}

/// The nearest directory that exists, starting at `path` and walking up: watching
/// it is how a root that appears *later* (or one that is deleted and recreated) is
/// noticed, since QFileSystemWatcher cannot watch a path that is not there.
std::string nearestExistingDirectory(const std::string& path) {
    std::error_code ec;
    std::filesystem::path candidate(absoluteNormalized(path));
    while (!candidate.empty() && !std::filesystem::is_directory(candidate, ec)) {
        if (!candidate.has_parent_path() || candidate.parent_path() == candidate) {
            break;
        }
        candidate = candidate.parent_path();
    }
    return std::filesystem::is_directory(candidate, ec) ? candidate.string() : std::string();
}

}  // namespace

bool PresetStore::isPresetFile(const QString& fileName) {
    const QString suffix = fileName.section('.', -1).toLower();
    return fileName.contains('.') && (suffix == QStringLiteral("milk") || suffix == QStringLiteral("milk2"));
}

PresetStore::PresetStore(QObject* parent) : QObject(parent) {
    m_rng.seed(std::random_device{}());
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kRescanDebounceMs);
    QObject::connect(&m_debounce, &QTimer::timeout, this, [this] { rescan(); });
    QObject::connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
                     [this] { scheduleRescan(); });
}

void PresetStore::setRoots(const std::vector<std::string>& roots) {
    std::vector<std::string> normalized;
    normalized.reserve(roots.size());
    for (const auto& root : roots) {
        if (!root.empty()) {
            normalized.push_back(absoluteNormalized(root));
        }
    }
    if (normalized == m_roots) {
        return;
    }
    m_roots = std::move(normalized);
    rescan();
}

bool PresetStore::rescan() {
    std::vector<std::string> found;
    std::vector<std::string> directories;

    for (const auto& root : m_roots) {
        std::error_code ec;
        const std::filesystem::path rootPath(root);
        if (std::filesystem::is_regular_file(rootPath, ec)) {
            // A file given directly as a root is a preset if it looks like one.
            if (isPresetFile(QString::fromStdString(rootPath.filename().string()))) {
                found.push_back(rootPath.string());
            }
            continue;
        }
        if (!std::filesystem::is_directory(rootPath, ec)) {
            continue;
        }
        directories.push_back(rootPath.string());
        std::filesystem::recursive_directory_iterator it(
            rootPath, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code entryEc;
            const std::filesystem::path& entry = it->path();
            // Hidden entries are skipped rather than walked, the same rule the
            // library scanner uses for user folders.
            const std::string name = entry.filename().string();
            if (!name.empty() && name.front() == '.') {
                if (it->is_directory(entryEc)) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (it->is_directory(entryEc)) {
                directories.push_back(entry.string());
            } else if (it->is_regular_file(entryEc) &&
                       isPresetFile(QString::fromStdString(entry.filename().string()))) {
                found.push_back(entry.string());
            }
        }
    }

    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());

    const bool changed = found != m_presets;
    m_presets = std::move(found);

    // Keep the selection valid; on a first scan (or after the selected preset was
    // deleted) fall back to the first entry.
    if (m_current.empty() ||
        std::find(m_presets.begin(), m_presets.end(), m_current) == m_presets.end()) {
        setCurrent(m_presets.empty() ? std::string{} : m_presets.front());
    }

    if (m_watcher.directories().isEmpty() || !directories.empty() || changed) {
        refreshWatchedDirectories();
    }
    if (changed) {
        emit listChanged();
    }
    return changed;
}

int PresetStore::currentIndex() const {
    const auto it = std::find(m_presets.begin(), m_presets.end(), m_current);
    return it == m_presets.end() ? -1 : static_cast<int>(std::distance(m_presets.begin(), it));
}

bool PresetStore::selectIndex(int index) {
    if (index < 0 || index >= static_cast<int>(m_presets.size())) {
        return false;
    }
    setCurrent(m_presets[static_cast<std::size_t>(index)]);
    return true;
}

bool PresetStore::selectPath(const std::string& path) {
    const auto it = std::find(m_presets.begin(), m_presets.end(), absoluteNormalized(path));
    return it != m_presets.end() &&
           selectIndex(static_cast<int>(std::distance(m_presets.begin(), it)));
}

bool PresetStore::next() {
    if (m_presets.empty()) {
        return false;
    }
    if (m_order == PresetOrder::Random && m_presets.size() > 1) {
        std::uniform_int_distribution<std::size_t> pick(0, m_presets.size() - 1);
        std::size_t index = pick(m_rng);
        while (static_cast<int>(index) == currentIndex()) {
            index = pick(m_rng);
        }
        return selectIndex(static_cast<int>(index));
    }
    const int index = currentIndex();
    return selectIndex(((index < 0 ? -1 : index) + 1) % static_cast<int>(m_presets.size()));
}

bool PresetStore::previous() {
    if (m_presets.empty()) {
        return false;
    }
    const int count = static_cast<int>(m_presets.size());
    const int index = currentIndex();
    return selectIndex(index < 0 ? count - 1 : (index - 1 + count) % count);
}

void PresetStore::setWatching(bool on) {
    if (on == isWatching()) {
        return;
    }
    if (!on) {
        m_debounce.stop();
        m_watcher.removePaths(m_watcher.directories());
        return;
    }
    refreshWatchedDirectories();
}

bool PresetStore::isWatching() const {
    return !m_watcher.directories().isEmpty() || m_watcher.files().size() > 0;
}

void PresetStore::refreshWatchedDirectories() {
    std::vector<std::string> wanted;  // rooted directories to watch
    for (const auto& root : m_roots) {
        if (const std::string existing = nearestExistingDirectory(root); !existing.empty()) {
            wanted.push_back(existing);
        }
    }
    for (const auto& preset : m_presets) {
        wanted.emplace_back(std::filesystem::path(preset).parent_path().string());
    }
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());

    QStringList wantedList;
    wantedList.reserve(static_cast<qsizetype>(wanted.size()));
    for (const auto& directory : wanted) {
        wantedList << QString::fromStdString(directory);
    }

    const QStringList current = m_watcher.directories();
    QStringList added = wantedList;
    for (const QString& present : current) {
        added.removeAll(present);
    }
    if (!added.isEmpty()) {
        m_watcher.addPaths(added);
    }
    QStringList stale = current;
    for (const QString& wantedDirectory : wantedList) {
        stale.removeAll(wantedDirectory);
    }
    if (!stale.isEmpty()) {
        m_watcher.removePaths(stale);
    }
}

void PresetStore::scheduleRescan() {
    m_debounce.start();
}

void PresetStore::setCurrent(std::string path) {
    if (path == m_current) {
        return;
    }
    m_current = std::move(path);
    emit currentChanged(m_current);
}

void PresetStore::setRandomSeed(std::uint32_t seed) {
    m_rng.seed(seed);
}

}  // namespace koalaamp::ui
