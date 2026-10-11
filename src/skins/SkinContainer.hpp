// SPDX-License-Identifier: MIT
//
// SkinContainer — the file set of a classic skin, however it was shipped:
// a `.wsz`/`.zip` archive or an extracted folder (docs/spec.md §5.1).
//
// Member lookup is by **lower-cased basename**, so `MAIN.BMP`, `Main.bmp` and
// `folder/main.bmp` all resolve to "main.bmp". Where several members match, the
// shallowest path wins and, at equal depth, the *last* one wins — the order
// Winamp gets when it extracts a ZIP over an existing folder (later entries
// overwrite earlier ones); Webamp's `getFileFromZip` encodes the same rule.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace koalaamp::skins {

/// One archive entry / file, with its raw bytes.
struct ContainerEntry {
    std::string name;      ///< Original name as stored (may contain a folder prefix).
    std::string basename;  ///< Lower-cased final path component, '/'-normalised.
    std::vector<std::uint8_t> data;
};

/// The member set of a skin container. Cheap to copy/move; never throws.
class SkinContainer {
public:
    /// Best match for `basename` (lower-cased, no directory part), or nullptr.
    const ContainerEntry* find(std::string_view basename) const;

    /// Raw bytes of `basename`, or nullptr when absent.
    const std::vector<std::uint8_t>* findData(std::string_view basename) const;

    bool contains(std::string_view basename) const { return find(basename) != nullptr; }
    bool empty() const { return m_entries.empty(); }
    std::size_t size() const { return m_entries.size(); }
    const std::vector<ContainerEntry>& entries() const { return m_entries; }

    /// Builds a container from already-read entries (used by tests and by the
    /// folder/archive loaders). `names` are normalised here.
    static SkinContainer fromEntries(std::vector<ContainerEntry> entries);

    /// Normalises an archive/directory name to its lookup key (lower-cased
    /// basename with `\` treated as `/`).
    static std::string normalizeBasename(std::string_view name);

private:
    std::vector<ContainerEntry> m_entries;
};

/// Loads `path` as a container: a directory (recursed one level) or a ZIP
/// archive. Returns nullopt when the path is missing/unreadable, a file that is
/// not a ZIP, or a directory with no readable members.
std::optional<SkinContainer> loadContainer(const std::filesystem::path& path);

/// Builds a container from a directory tree (one level of nesting). Exposed for
/// tests; `loadContainer` calls it for directories.
std::optional<SkinContainer> loadContainerFromDirectory(const std::filesystem::path& dir);

}  // namespace koalaamp::skins
