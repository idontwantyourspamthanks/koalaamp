// SPDX-License-Identifier: MIT
// servers — the remembered server list (Jellyfin today, Plex later).
//
// The connection manager needs somewhere to put "these are the servers I know and
// this is the account I signed in with", and nothing more: the access token is a
// session secret and never touches this store (AGENTS.md forbids committed secrets;
// the token is re-acquired by the manager and refreshed on 401). The store keeps
// identity only, so a leaked `servers.json` leaks no credentials.
//
// One JSON file, written atomically (temp + rename) so a crash mid-save leaves the
// previous book intact. Missing file = empty book; malformed file = empty book plus
// `lastError()`, never an exception.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace koalaamp::servers {

/// One remembered connection. `serverId` is the server's own id (Jellyfin
/// `ServerId`) — the stable key for upsert/remove — and `baseUrl` the URL the user
/// entered, normalised without a trailing slash.
struct ServerEntry {
    std::string serverId;
    std::string name;
    std::string baseUrl;
    std::string userId;
    std::string userName;
    /// Per-(machine, user) DeviceId. Kept so a reconnect re-binds the same device
    /// and Jellyfin does not invalidate the previous token (koalapaw's trap).
    std::string deviceId;

    bool operator==(const ServerEntry&) const = default;
};

class ServerBook {
public:
    /// `file` is the JSON path; the parent directory is created on save.
    explicit ServerBook(std::filesystem::path file);

    /// Load from disk. Returns false when the file is missing or unreadable; a
    /// malformed file leaves an empty book and sets `lastError()`.
    bool load();
    /// Atomic save. Returns false and sets `lastError()` on an I/O failure.
    bool save() const;

    const std::vector<ServerEntry>& entries() const { return entries_; }

    /// The active entry, or nullptr when none is selected (or it was removed).
    const ServerEntry* active() const;

    /// Adds the entry, or replaces the one with the same `serverId`, and makes it
    /// active. Returns its index.
    std::size_t upsert(ServerEntry entry);

    /// Removes by `serverId`; clears the active pointer when it named that entry.
    bool remove(const std::string& serverId);

    /// Selects by `serverId`; false when unknown.
    bool setActive(const std::string& serverId);
    void clearActive() { activeId_.clear(); }

    const std::string& lastError() const { return lastError_; }

private:
    std::filesystem::path file_;
    std::vector<ServerEntry> entries_;
    std::string activeId_;
    mutable std::string lastError_;
};

} // namespace koalaamp::servers
