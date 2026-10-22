// SPDX-License-Identifier: MIT
//
// PresetStore — the visualizer's preset library: a recursive scan of one or more
// roots for MilkDrop presets (`.milk` / `.milk2`), kept current by a filesystem
// watcher, plus the ordered/random selection the window cycles through.
//
// VZ-1 task 3. The list is rebuilt on demand and (debounced) when a watched
// directory changes, so a user who drops presets into a folder sees them without a
// restart. Selection is deliberately timer-free: the cycling *policy* — interval,
// on-screen control, the name overlay — is task 4's, and this class only answers
// "what is next".
//
// No projectM dependency: paths in, paths out (the canvas does the loading, and it
// queues loads into paintGL because a load needs the GL context — the FE-14 rule).
//
// docs/tickets/VZ-1-visualizer.md task 3; docs/spec.md §6.1.
#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace koalaamp::ui {

/// How `next()` picks: through the sorted list, or at random (never repeating the
/// current preset while more than one exists).
enum class PresetOrder { Ordered, Random };

class PresetStore : public QObject {
    Q_OBJECT

public:
    /// `.milk`/`.milk2`, case-insensitively — real packs use both.
    static bool isPresetFile(const QString& fileName);
    /// The name a preset is announced by: the file's base name with underscores
    /// turned into spaces ("01_phat_RippleNebula9.milk" -> "01 phat RippleNebula9").
    /// `.milk` has no author/name field to cache; cycling's only metadata is this
    /// filename-derived display name, so there is no file read on a switch.
    static std::string presetDisplayName(const std::string& path);

    explicit PresetStore(QObject* parent = nullptr);

    // --- roots and scanning --------------------------------------------------
    /// Roots scanned recursively. A root that does not exist yet is kept, so a
    /// folder created later is picked up by the watcher.
    void setRoots(const std::vector<std::string>& roots);
    const std::vector<std::string>& roots() const { return m_roots; }

    /// Rebuilds the list now. Returns true when the set of presets changed.
    bool rescan();

    /// The presets found, sorted and unique, absolute.
    const std::vector<std::string>& presets() const { return m_presets; }
    std::size_t size() const { return m_presets.size(); }
    bool empty() const { return m_presets.empty(); }

    // --- selection -----------------------------------------------------------
    void setOrder(PresetOrder order) { m_order = order; }
    PresetOrder order() const { return m_order; }
    /// Seeds the random order (tests); the default is seeded from the system.
    void setRandomSeed(std::uint32_t seed);

    /// The selected preset, empty when the store has none.
    const std::string& current() const { return m_current; }
    /// Index of `current()` in `presets()`, or -1.
    int currentIndex() const;

    bool selectIndex(int index);
    bool selectPath(const std::string& path);
    /// The next preset per `order()`. False when there is nothing to move to.
    bool next();
    /// The previous preset in list order. False when there is nothing to move to.
    bool previous();

    // --- watching ------------------------------------------------------------
    /// Watch the roots and every directory the scan walked. Changes are debounced
    /// (`kRescanDebounceMs`) because editors write several events per save.
    void setWatching(bool on);
    bool isWatching() const;

    /// Debounce applied to watcher events before a rescan.
    static constexpr int kRescanDebounceMs = 500;

signals:
    /// The set of presets changed (added, removed, or re-listed).
    void listChanged();
    /// The selection moved (or was dropped because its file vanished).
    void currentChanged(const std::string& path);

private:
    void refreshWatchedDirectories();
    void scheduleRescan();
    void setCurrent(std::string path);

    std::vector<std::string> m_roots;
    std::vector<std::string> m_presets;
    std::string m_current;
    PresetOrder m_order = PresetOrder::Ordered;
    std::mt19937 m_rng;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};

}  // namespace koalaamp::ui
