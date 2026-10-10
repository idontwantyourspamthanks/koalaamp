// SPDX-License-Identifier: MIT
// jellyfin — client device identity.
//
// Transliterated from koalapaw. The server permits **one access token per
// DeviceId**, so every user needs their own id or signing one in evicts the
// others (AuthInterceptor.kt:17; JellyfinGateway.kt `apiFor` comments).
//
// koalapaw's derivation (EncryptedServerStore.kt `installId`,
// ManageUsersViewModel/FamilySetupViewModel `deviceIdForUser`) is:
//   * one install-level id, a random UUID minted once and persisted;
//   * a per-profile id, reusing the stored one or minting a fresh random UUID.
// A random UUID is stable only because it is persisted per profile. JellyAmp
// instead derives the per-user id deterministically as
// SHA-256(machineId ":" userId) — the ticket's formula — so it survives a lost
// profile store while keeping the same invariants: stable per user, distinct
// per user, never the install id itself. Recorded as a re-shaping in
// docs/spikes/koalapaw-port.md.

#pragma once

#include <string>
#include <string_view>

namespace koalaamp::servers::jellyfin {

/// Deterministic per-user DeviceId: lowercase hex SHA-256 of
/// `machineId + ":" + userKey`. `userKey` identifies the user on this server —
/// the Jellyfin user id, or the user name at sign-in time (the id is not known
/// until after the first authenticate). Distinct users and distinct machines
/// map to distinct ids; re-authenticating the same user reuses the same id
/// (the token-invalidation trap this must not trip).
std::string deriveDeviceId(std::string_view machineId, std::string_view userKey);

/// A stable, process-independent random UUIDv4 string (koalapaw uses
/// `UUID.randomUUID()` for its install id).
std::string randomUuid();

/// Reads (or, on first run, mints and persists) the install-level machine id.
/// Ported from koalapaw `EncryptedServerStore.kt`: the id is generated once and
/// reused across launches; a missing/unreadable file yields an in-memory id
/// for this session only.
class MachineIdStore {
public:
    /// `path` is the file the id is persisted in. Empty = derive from
    /// `$XDG_STATE_HOME`/`$HOME`; tests pass an explicit temp path.
    explicit MachineIdStore(std::string path = {});

    /// Returns the persisted id, minting + persisting one if the file is
    /// absent. Never throws across the boundary: on I/O failure the minted id
    /// is returned but not persisted.
    std::string machineId();

    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::string cached_;
};

/// Default on-disk location for the machine id (XDG state dir on Linux,
/// Application Support on macOS, %LOCALAPPDATA% on Windows).
std::string defaultMachineIdPath();

} // namespace koalaamp::servers::jellyfin
