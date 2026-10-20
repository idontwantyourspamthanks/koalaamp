// SPDX-License-Identifier: MIT
// scrobble — the credential-store seam and the slot names scrobbling uses.
//
// This is the same three-method contract as `koalaamp::servers::SecretStore`
// (SV-6): store/load/remove on opaque string keys. The servers target links
// Qt::DBus on Linux, so reusing its *header* would make this Qt-free module
// depend on Qt through CMake. A one-line adapter can forward these three
// virtuals to the servers store when the UI wires both modules:
//
//   class ServersSecretStore final : public ISecretStore { ... };
//
// Keeping the seam here also means tests can use MemorySecretStore, and the
// Last.fm session key / ListenBrainz token never touch a settings file.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace koalaamp::scrobble {

class ISecretStore {
public:
    virtual ~ISecretStore() = default;

    virtual bool store(std::string_view key, std::string_view secret) = 0;
    virtual std::optional<std::string> load(std::string_view key) const = 0;
    virtual bool remove(std::string_view key) = 0;
};

/// In-memory stand-in; tests and the "no keyring available" fallback.
class MemorySecretStore final : public ISecretStore {
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

/// Keychain slots. The Last.fm session key has infinite lifetime until the user
/// revokes the application; the ListenBrainz token comes from the user's
/// settings page. Neither is ever written to the scrobble queue.
inline constexpr std::string_view kLastFmSessionKeySlot = "scrobble/lastfm/session";
inline constexpr std::string_view kLastFmUsernameSlot = "scrobble/lastfm/username";
inline constexpr std::string_view kListenBrainzTokenSlot = "scrobble/listenbrainz/token";

inline std::string loadLastFmSessionKey(const ISecretStore& store) {
    return store.load(kLastFmSessionKeySlot).value_or(std::string{});
}

inline bool rememberLastFmSession(ISecretStore& store, std::string_view username,
                                  std::string_view sessionKey) {
    return store.store(kLastFmSessionKeySlot, sessionKey) &&
           store.store(kLastFmUsernameSlot, username);
}

inline void forgetLastFmSession(ISecretStore& store) {
    store.remove(kLastFmSessionKeySlot);
    store.remove(kLastFmUsernameSlot);
}

inline std::string loadListenBrainzToken(const ISecretStore& store) {
    return store.load(kListenBrainzTokenSlot).value_or(std::string{});
}

inline bool rememberListenBrainzToken(ISecretStore& store, std::string_view token) {
    return store.store(kListenBrainzTokenSlot, token);
}

inline void forgetListenBrainzToken(ISecretStore& store) {
    store.remove(kListenBrainzTokenSlot);
}

} // namespace koalaamp::scrobble
