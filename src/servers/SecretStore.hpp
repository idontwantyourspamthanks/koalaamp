// SPDX-License-Identifier: MIT
// servers — where a Jellyfin access token lives between launches.
//
// The server book stays identity-only (no token in servers.json). The token from
// a password login or a QuickConnect approval is stored here, keyed by the
// server id, and read back on the next launch. The production store is the OS
// credential store; tests use MemorySecretStore.

#pragma once

#include "servers/IServerSource.hpp"
#include "servers/ServerBook.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace koalaamp::servers {

/// Opaque credential slot. `jellyfin/<server id>` is the key for one account.
std::string sessionSecretKey(ServerBackend backend, std::string_view serverId,
                             std::string_view baseUrl);

class SecretStore {
public:
    virtual ~SecretStore() = default;

    /// Replaces any existing value. False when the backing store refused the write.
    virtual bool store(std::string_view key, std::string_view secret) = 0;
    /// Nullopt when the key is absent or the store cannot be read.
    virtual std::optional<std::string> load(std::string_view key) const = 0;
    /// True when an entry was removed. Absent is success as well as a no-op false
    /// only when the caller needs to know; implementations may return false if missing.
    virtual bool remove(std::string_view key) = 0;
};

/// In-memory stand-in. Dialog and book tests use it so a credential never touches
/// the OS keyring or the book file.
class MemorySecretStore final : public SecretStore {
public:
    bool store(std::string_view key, std::string_view secret) override {
        entries_[std::string(key)] = std::string(secret);
        return true;
    }

    std::optional<std::string> load(std::string_view key) const override {
        const auto it = entries_.find(std::string(key));
        if (it == entries_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool remove(std::string_view key) override { return entries_.erase(std::string(key)) > 0; }

private:
    std::map<std::string, std::string> entries_;
};

/// Writes `auth.accessToken` under the server's key. False when there is nothing
/// to store or the backing store refused.
bool rememberSession(SecretStore& store, const ServerAuth& auth);

/// Drops the key for this session. Missing entries are not an error.
void forgetSession(SecretStore& store, ServerBackend backend, std::string_view serverId,
                   std::string_view baseUrl);

/// The active book's identity plus the keychain token, or nullopt when either is
/// missing. Does not contact the server — a later 401 drops the key.
std::optional<ServerAuth> restoreSession(const ServerBook& book, const SecretStore& store);

/// Windows Credential Manager, macOS login keychain, or on Linux the Secret
/// Service with a KWallet fallback when that service is not running.
std::unique_ptr<SecretStore> makePlatformSecretStore();

} // namespace koalaamp::servers
