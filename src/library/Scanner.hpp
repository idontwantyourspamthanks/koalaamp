// SPDX-License-Identifier: MIT
// library — recursive scanner + polling watcher (LI-1).
//
// Headless and Qt-free: the UI runs a scan on a worker and feeds the results into
// `LibraryDb` (LI-2). TagLib supplies the tag/audio-property field set; files TagLib
// cannot parse (MOD family) still enter the library with filename-derived metadata.

#pragma once

#include "library/ITrackSource.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace koalaamp::library {

/// One file with the metadata the DB stores alongside `Track`.
struct ScannedTrack {
    Track track;
    std::int64_t mtime{0}; ///< last-write timestamp in ns since the filesystem epoch
    std::uint64_t size{0};
    std::string format; ///< lower-case extension without the dot
    int sampleRate{0};
    int channels{0};
    int bpm{0};
    std::string composer;
    std::string publisher;
    std::string comment;
    std::string artPath; ///< folder cover path when one exists
};

/// Change-detection stamp: nanosecond mtime + byte size.
struct FileStamp {
    std::int64_t mtime{0};
    std::uint64_t size{0};

    bool operator==(const FileStamp&) const = default;
};

struct ScanSummary {
    std::size_t seen{0};
    std::size_t added{0};
    std::size_t updated{0};
    std::size_t unchanged{0};
    std::size_t unreadable{0}; ///< files that vanished/permission-failed mid-scan
    bool cancelled{false};
};

struct ScanCallbacks {
    /// Called for added/changed files only (never for unchanged ones).
    std::function<void(const ScannedTrack&)> onTrack;
    /// The DB's stamp for a path, or nullopt when the file is new.
    std::function<std::optional<FileStamp>(const std::string& path)> known;
    /// Polled before each file; true aborts the scan.
    std::function<bool()> cancelled;
};

/// Recursively scans `roots`. Hidden entries are skipped, as is any directory
/// containing a file named `.ignore`; symlinks are not followed.
ScanSummary scanRoots(const std::vector<std::filesystem::path>& roots,
                      const ScanCallbacks& callbacks);

/// Reads one file. Returns nullopt only when the file does not exist / cannot be
/// stat'ed; an unparsable file still yields filename-derived metadata.
std::optional<ScannedTrack> readTrack(const std::filesystem::path& path);

/// True for the spec §7.1 audio whitelist (`mp3 flac ogg …`).
bool isAudioExtension(const std::string& extension);

/// Polling watcher (default 2 s) with a settle window. `onChange` runs on the
/// watcher thread; it is called at most once per settled change batch.
class LibraryWatcher {
public:
    explicit LibraryWatcher(std::vector<std::filesystem::path> roots,
                            std::chrono::milliseconds interval = std::chrono::seconds(2));
    ~LibraryWatcher();
    LibraryWatcher(const LibraryWatcher&) = delete;
    LibraryWatcher& operator=(const LibraryWatcher&) = delete;

    void setOnChange(std::function<void()> callback);
    void start();
    void stop();
    bool running() const { return running_.load(std::memory_order_acquire); }

private:
    void run();

    std::vector<std::filesystem::path> roots_;
    std::chrono::milliseconds interval_;
    std::function<void()> onChange_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
};

} // namespace koalaamp::library
