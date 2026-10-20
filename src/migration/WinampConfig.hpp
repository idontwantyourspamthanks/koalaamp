// SPDX-License-Identifier: MIT
// migration — Winamp path/config readers (MI-1).
//
// `paths.ini` controls where the real ini/playlists live; `winamp.ini` carries
// the player state the wizard offers to import. Both are read-only.

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace koalaamp::migration {

class IniFile {
public:
    /// Missing/unreadable file yields an empty ini (never throws).
    static IniFile read(const std::filesystem::path& path);

    /// Section/key lookup, case-insensitive; empty when absent.
    std::string get(std::string_view section, std::string_view key,
                    std::string_view fallback = {}) const;
    bool has(std::string_view section, std::string_view key) const;

private:
    std::map<std::string, std::map<std::string, std::string>, std::less<>> sections_;
};

struct WinampPaths {
    std::filesystem::path winampDir;
    std::filesystem::path iniDir;
    std::filesystem::path iniFile;
    std::filesystem::path playlistDir;
    std::filesystem::path skinsDir;

    bool found() const { return !winampDir.empty(); }
};

/// Uses `explicitDir` when supplied, otherwise probes `%APPDATA%/Winamp` and the
/// common Wine prefix. Honours `paths.ini` (`inidir`, `inifile`, `m3udir`).
WinampPaths detectWinampPaths(const std::filesystem::path& explicitDir = {});

struct WinampState {
    int volume{0}; ///< config_volume, 0..255 as Winamp stores it
    int preamp{31};
    int eqType{0};
    bool shuffle{false};
    bool repeat{false};
    bool useEq{false};
    bool autoloadEq{false};
    bool limiter{true};
    bool windowShade{false};
    std::string skin;
    std::string titleFormat;
};

WinampState readWinampState(const std::filesystem::path& iniFile);

} // namespace koalaamp::migration
