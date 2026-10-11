// SPDX-License-Identifier: MIT
#include "skins/SkinContainer.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

#include "skins/ZipReader.hpp"

namespace koalaamp::skins {
namespace {

// Hard caps so a hostile/corrupt container cannot exhaust memory (fuzz target).
constexpr std::size_t kMaxEntries = 8192;
constexpr std::size_t kMaxFileBytes = 32u * 1024u * 1024u;

/// Directory depth of a normalised name: 0 for "main.bmp", 1 for "a/main.bmp".
int depthOf(std::string_view name) {
    return static_cast<int>(std::count(name.begin(), name.end(), '/'));
}

std::optional<std::vector<std::uint8_t>> readFileBytes(const std::filesystem::path& p,
                                                       std::error_code& ec) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        ec = std::make_error_code(std::errc::io_error);
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (bytes.size() > kMaxFileBytes) {
        return std::nullopt;
    }
    return bytes;
}

void addDirectoryEntry(std::vector<ContainerEntry>& out, const std::filesystem::path& file,
                       const std::string& name) {
    if (out.size() >= kMaxEntries) {
        return;
    }
    std::error_code ec;
    const auto bytes = readFileBytes(file, ec);
    if (!bytes) {
        return;  // unreadable or oversized member: skip, never fatal
    }
    ContainerEntry entry;
    entry.name = name;
    entry.basename = SkinContainer::normalizeBasename(name);
    entry.data = *bytes;
    out.push_back(std::move(entry));
}

}  // namespace

std::string SkinContainer::normalizeBasename(std::string_view name) {
    std::string base(name);
    const auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos) {
        base = base.substr(slash + 1);
    }
    std::transform(base.begin(), base.end(), base.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return base;
}

SkinContainer SkinContainer::fromEntries(std::vector<ContainerEntry> entries) {
    SkinContainer c;
    c.m_entries.reserve(entries.size());
    for (auto& e : entries) {
        if (c.m_entries.size() >= kMaxEntries) {
            break;
        }
        e.basename = normalizeBasename(e.name);
        c.m_entries.push_back(std::move(e));
    }
    return c;
}

const ContainerEntry* SkinContainer::find(std::string_view basename) const {
    // Lower-case the key once; only ASCII names appear in classic skins.
    std::string key(basename);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const ContainerEntry* best = nullptr;
    int bestDepth = 0;
    for (const auto& e : m_entries) {
        if (e.basename != key) {
            continue;
        }
        const int depth = depthOf(e.name);
        // Shallowest path wins; at equal depth the later member wins (Winamp's
        // extract-over disk order).
        if (best == nullptr || depth <= bestDepth) {
            best = &e;
            bestDepth = depth;
        }
    }
    return best;
}

const std::vector<std::uint8_t>* SkinContainer::findData(std::string_view basename) const {
    const auto* e = find(basename);
    return e == nullptr ? nullptr : &e->data;
}

std::optional<SkinContainer> loadContainerFromDirectory(const std::filesystem::path& dir) {
    std::vector<ContainerEntry> entries;
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
        return std::nullopt;
    }
    for (const auto& entry : it) {
        if (entries.size() >= kMaxEntries) {
            break;
        }
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file(ec)) {
            addDirectoryEntry(entries, entry.path(), name);
        } else if (entry.is_directory(ec)) {
            // One level of nesting only (docs/spec.md §5.1).
            std::error_code sub;
            std::filesystem::directory_iterator inner(entry.path(), sub);
            if (sub) {
                continue;
            }
            for (const auto& child : inner) {
                if (entries.size() >= kMaxEntries) {
                    break;
                }
                if (child.is_regular_file(sub)) {
                    addDirectoryEntry(entries, child.path(),
                                      name + "/" + child.path().filename().string());
                }
            }
        }
    }
    if (entries.empty()) {
        return std::nullopt;
    }
    return SkinContainer::fromEntries(std::move(entries));
}

std::optional<SkinContainer> loadContainer(const std::filesystem::path& path) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec) || ec) {
        return std::nullopt;
    }
    if (std::filesystem::is_directory(path, ec) && !ec) {
        return loadContainerFromDirectory(path);
    }

    auto bytes = readFileBytes(path, ec);
    if (!bytes) {
        return std::nullopt;
    }
    // `.wsz` is a renamed ZIP; anything else (a `.wsz` holding a folder dump,
    // a text file, a truncated download) fails here and the caller reports it.
    auto entries = readZip(*bytes);
    if (!entries) {
        return std::nullopt;
    }
    std::vector<ContainerEntry> converted;
    converted.reserve(entries->size());
    for (auto& e : *entries) {
        ContainerEntry ce;
        ce.name = std::move(e.name);
        ce.basename = std::move(e.basename);
        ce.data = std::move(e.data);
        converted.push_back(std::move(ce));
    }
    return SkinContainer::fromEntries(std::move(converted));
}

}  // namespace koalaamp::skins
