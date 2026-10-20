// SPDX-License-Identifier: MIT
// migration — Winamp path/config readers. See WinampConfig.hpp.

#include "migration/WinampConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace koalaamp::migration {
namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    const auto first = std::find_if(value.begin(), value.end(), notSpace);
    const auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return first < last ? std::string(first, last) : std::string{};
}

bool parseBool(const std::string& value, bool fallback) {
    const std::string normalized = lower(trim(value));
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    return fallback;
}

int parseInt(const std::string& value, int fallback) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(trim(value), &used, 0);
        return used == 0 ? fallback : parsed;
    } catch (const std::exception&) {
        return fallback;
    }
}

std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::filesystem::path resolve(const std::filesystem::path& base, const std::string& value) {
    if (value.empty()) {
        return {};
    }
    std::filesystem::path path = unquote(value);
    return path.is_absolute() ? path : base / path;
}

std::filesystem::path appDataRoot() {
    if (const char* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0') {
        return std::filesystem::path(appData) / "Winamp";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        const char* user = std::getenv("USER");
        const std::string profile = user != nullptr && *user != '\0' ? user : "user";
        return std::filesystem::path(home) / ".wine" / "drive_c" / "users" / profile / "AppData" /
               "Roaming" / "Winamp";
    }
    return {};
}

} // namespace

IniFile IniFile::read(const std::filesystem::path& path) {
    IniFile ini;
    std::ifstream in(path);
    if (!in) {
        return ini;
    }
    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(std::move(line));
        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        ini.sections_[section][lower(trim(line.substr(0, equals)))] = trim(line.substr(equals + 1));
    }
    return ini;
}

std::string IniFile::get(std::string_view section, std::string_view key,
                         std::string_view fallback) const {
    const auto sectionIt = sections_.find(lower(section));
    if (sectionIt == sections_.end()) {
        return std::string(fallback);
    }
    const auto keyIt = sectionIt->second.find(lower(key));
    return keyIt == sectionIt->second.end() ? std::string(fallback) : keyIt->second;
}

bool IniFile::has(std::string_view section, std::string_view key) const {
    const auto sectionIt = sections_.find(lower(section));
    return sectionIt != sections_.end() &&
           sectionIt->second.find(lower(key)) != sectionIt->second.end();
}

WinampPaths detectWinampPaths(const std::filesystem::path& explicitDir) {
    WinampPaths paths;
    std::error_code error;
    paths.winampDir =
        explicitDir.empty() ? appDataRoot() : std::filesystem::absolute(explicitDir, error);
    if (paths.winampDir.empty() || !std::filesystem::is_directory(paths.winampDir, error)) {
        paths.winampDir.clear();
        return paths;
    }

    const IniFile pathsIni = IniFile::read(paths.winampDir / "paths.ini");
    paths.iniDir = resolve(paths.winampDir, pathsIni.get("winamp", "inidir"));
    if (paths.iniDir.empty()) {
        paths.iniDir = paths.winampDir;
    }
    paths.iniFile = resolve(paths.iniDir, pathsIni.get("winamp", "inifile", "winamp.ini"));
    if (paths.iniFile.empty()) {
        paths.iniFile = paths.iniDir / "winamp.ini";
    }
    paths.playlistDir = resolve(paths.winampDir, pathsIni.get("winamp", "m3udir"));
    if (paths.playlistDir.empty()) {
        paths.playlistDir = paths.winampDir;
    }
    paths.skinsDir = paths.winampDir / "Skins";
    return paths;
}

WinampState readWinampState(const std::filesystem::path& iniFile) {
    const IniFile ini = IniFile::read(iniFile);
    WinampState state;
    state.volume = std::clamp(parseInt(ini.get("winamp", "config_volume"), state.volume), 0, 255);
    state.preamp = std::clamp(parseInt(ini.get("winamp", "config_preamp"), state.preamp), 0, 63);
    state.eqType = parseInt(ini.get("winamp", "eq_type"), state.eqType);
    state.shuffle = parseBool(ini.get("winamp", "config_shuffle"), state.shuffle);
    state.repeat = parseBool(ini.get("winamp", "config_repeat"), state.repeat);
    state.useEq = parseBool(ini.get("winamp", "config_use_eq"), state.useEq);
    state.autoloadEq = parseBool(ini.get("winamp", "config_autoload_eq"), state.autoloadEq);
    state.limiter = parseBool(ini.get("winamp", "eq_limiter"), state.limiter);
    state.windowShade = parseBool(ini.get("winamp", "config_windowshade"), state.windowShade);
    state.skin = unquote(ini.get("winamp", "config_skin"));
    state.titleFormat = unquote(ini.get("winamp", "config_titlefmt"));
    return state;
}

} // namespace koalaamp::migration
