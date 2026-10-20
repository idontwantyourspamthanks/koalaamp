// SPDX-License-Identifier: MIT
// scrobble — persistent pending scrobbles (offline retry queue).
//
// One small JSON file next to the rest of the app's state. Writes go to
// `<file>.tmp` and are renamed into place, so a crash mid-write leaves the last
// good queue. Entries are deduplicated by `listenDedupKey`, which includes the
// service and the listen's start timestamp.
//
// The queue stores public listen metadata only. API keys, shared secrets,
// session keys and ListenBrainz tokens never enter this file — they belong in
// the OS credential store (SecretStore.hpp).

#pragma once

#include "scrobble/ScrobbleTrack.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace koalaamp::scrobble {

struct PendingScrobble {
    ListenService service{ListenService::LastFm};
    std::string dedupKey;
    ScrobbleTrack track;
    std::int64_t timestampUnix{0};
    /// Failed flush attempts. Diagnostics only; the coordinator sends regardless.
    int attempts{0};
};

class ScrobbleQueue {
public:
    explicit ScrobbleQueue(std::filesystem::path file);

    /// Reads the queue. A missing file is an empty queue (true). A malformed
    /// file is tolerated: the queue is left empty, entries that cannot be
    /// parsed are skipped, and the call returns false.
    bool load();
    /// Atomic write via temp + rename. Creates the parent directory.
    bool save() const;

    /// False when the dedup key is already pending. Empty keys are derived from
    /// service + track + timestamp.
    bool enqueue(PendingScrobble entry);
    bool remove(std::string_view dedupKey);
    bool contains(std::string_view dedupKey) const;
    /// Increments the retry counter for one entry; false when absent.
    bool recordAttempt(std::string_view dedupKey);

    const std::vector<PendingScrobble>& entries() const { return entries_; }
    std::size_t size() const { return entries_.size(); }
    const std::filesystem::path& path() const { return file_; }

    /// Hands every pending entry to `send` as one batch. On success the queue is
    /// cleared; on failure every entry's attempt counter is incremented and the
    /// queue is kept. Returns the number of entries accepted.
    int flush(const std::function<bool(const std::vector<PendingScrobble>&)>& send);

private:
    std::filesystem::path file_;
    std::vector<PendingScrobble> entries_;
    std::set<std::string> keys_;
};

} // namespace koalaamp::scrobble
