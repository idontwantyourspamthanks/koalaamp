// SPDX-License-Identifier: MIT
// servers — the remembered server list. See ServerBook.hpp.

#include "servers/ServerBook.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>

namespace koalaamp::servers {
namespace {

using json = nlohmann::json;

ServerEntry entryFromJson(const json& j) {
    ServerEntry entry;
    entry.serverId = j.value("serverId", std::string{});
    entry.name = j.value("name", std::string{});
    entry.baseUrl = j.value("baseUrl", std::string{});
    entry.userId = j.value("userId", std::string{});
    entry.userName = j.value("userName", std::string{});
    entry.deviceId = j.value("deviceId", std::string{});
    entry.backend =
        j.value("backend", std::string{}) == "plex" ? ServerBackend::Plex : ServerBackend::Jellyfin;
    return entry;
}

json entryToJson(const ServerEntry& entry) {
    return json{{"serverId", entry.serverId},
                {"name", entry.name},
                {"baseUrl", entry.baseUrl},
                {"userId", entry.userId},
                {"userName", entry.userName},
                {"deviceId", entry.deviceId},
                {"backend", entry.backend == ServerBackend::Plex ? "plex" : "jellyfin"}};
}

std::string trimmedUrl(std::string url) {
    while (!url.empty() && (url.back() == '/' || url.back() == ' ')) {
        url.pop_back();
    }
    return url;
}

} // namespace

ServerBook::ServerBook(std::filesystem::path file) : file_(std::move(file)) {}

bool ServerBook::load() {
    lastError_.clear();
    entries_.clear();
    activeId_.clear();

    std::ifstream in(file_);
    if (!in) {
        return false; // No book yet is not an error.
    }
    try {
        const json j = json::parse(in);
        if (const auto it = j.find("servers"); it != j.end() && it->is_array()) {
            for (const auto& item : *it) {
                if (item.is_object()) {
                    ServerEntry entry = entryFromJson(item);
                    if (!entry.serverId.empty()) {
                        entries_.push_back(std::move(entry));
                    }
                }
            }
        }
        activeId_ = j.value("active", std::string{});
        if (active() == nullptr) {
            activeId_.clear();
        }
        return true;
    } catch (const std::exception& e) {
        lastError_ = std::string{"Malformed servers file: "} + e.what();
        entries_.clear();
        activeId_.clear();
        return false;
    }
}

bool ServerBook::save() const {
    lastError_.clear();
    std::error_code ec;
    if (!file_.parent_path().empty()) {
        std::filesystem::create_directories(file_.parent_path(), ec);
        if (ec) {
            lastError_ = "Cannot create " + file_.parent_path().string() + ": " + ec.message();
            return false;
        }
    }

    json servers = json::array();
    for (const auto& entry : entries_) {
        servers.push_back(entryToJson(entry));
    }
    const json document{{"servers", servers}, {"active", activeId_}};

    // Atomic: write a sibling temp file, then rename over the target.
    auto temp = file_;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            lastError_ = "Cannot open " + temp.string();
            return false;
        }
        out << document.dump(2) << "\n";
        if (!out) {
            lastError_ = "Cannot write " + temp.string();
            return false;
        }
    }
    std::filesystem::rename(temp, file_, ec);
    if (ec) {
        lastError_ = "Cannot replace " + file_.string() + ": " + ec.message();
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

const ServerEntry* ServerBook::active() const {
    if (activeId_.empty()) {
        return nullptr;
    }
    for (const auto& entry : entries_) {
        if (entry.serverId == activeId_) {
            return &entry;
        }
    }
    return nullptr;
}

std::size_t ServerBook::upsert(ServerEntry entry) {
    entry.baseUrl = trimmedUrl(std::move(entry.baseUrl));
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].serverId == entry.serverId) {
            entries_[i] = std::move(entry);
            activeId_ = entries_[i].serverId;
            return i;
        }
    }
    activeId_ = entry.serverId;
    entries_.push_back(std::move(entry));
    return entries_.size() - 1;
}

bool ServerBook::remove(const std::string& serverId) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->serverId == serverId) {
            entries_.erase(it);
            if (activeId_ == serverId) {
                activeId_.clear();
            }
            return true;
        }
    }
    return false;
}

bool ServerBook::setActive(const std::string& serverId) {
    for (const auto& entry : entries_) {
        if (entry.serverId == serverId) {
            activeId_ = serverId;
            return true;
        }
    }
    return false;
}

} // namespace koalaamp::servers
