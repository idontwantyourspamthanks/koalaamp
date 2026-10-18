// SPDX-License-Identifier: MIT
//
// SkinSet — the assembled model of a classic skin (docs/spec.md §5.1): every
// canonical bitmap decoded into a `Skin`, the three text configs parsed, cursor
// paths collected, and a diagnostic log of what was missing or malformed.
//
// Assembly is deliberately forgiving, matching Winamp/Webamp: `main.bmp` is the
// only hard requirement; every other member is optional and falls back to a
// built-in default (viscolor/pledit) or to "absent sheet" (rendered as a hole).
// A container without a readable `main.bmp` is rejected with `nullopt` — never
// an exception.
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "skins/Skin.hpp"
#include "skins/SkinContainer.hpp"
#include "skins/SkinInfo.hpp"
#include "skins/SkinText.hpp"

namespace koalaamp::skins {

/// One member of the canonical classic-skin file set.
struct SkinFileSpec {
    std::string_view name;      ///< Lower-case basename ("main.bmp").
    bool required;              ///< Winamp cannot use the skin without it.
    std::string_view fallback;  ///< Behaviour when absent ("" = rendered as hole).
};

/// The canonical file list (docs/spec.md §5.1, docs/research/skins.md §1.2-1.3).
const std::vector<SkinFileSpec>& canonicalSkinFiles();

/// Reference size of a canonical sheet, or a null size when unspecified/unknown.
QSize referenceSheetSize(std::string_view name);

/// The complete classic-skin surface the UI renders from.
class SkinSet {
public:
    SkinInfo info;
    Skin sheets;
    VisColor visColor;
    PleditColors pleditColors;
    Region region;
    /// Upper-case cursor basenames present in the container ("CLOSE.CUR").
    std::vector<std::string> cursors;
    /// Raw text of non-canonical `.txt`/`.ini` members (readme, mb settings...).
    std::map<std::string, std::string, std::less<>> otherText;
    /// Canonical members that were absent (sorted).
    std::vector<std::string> missingOptional;
    /// Recoverable problems: undecodable/undersized art, unparsable configs.
    std::vector<std::string> warnings;

    bool hasSheet(std::string_view name) const;
    QSize sheetSize(std::string_view name) const;
    bool hasCursor(std::string_view name) const;
    /// True when the mandatory `main.bmp` decoded to a non-empty sheet.
    bool isComplete() const { return hasSheet("MAIN") && info.mainWidth > 0; }
    /// Comma-joined `missingOptional` for a single log line.
    std::string missingSummary() const;

    /// Adds a diagnostic; deduplicated so repeated quirks stay readable.
    void warn(std::string message);
};

/// Loads a skin from a `.wsz`/`.zip` archive or an extracted folder. Returns
/// nullopt when the container cannot be read or has no usable `main.bmp`.
std::optional<SkinSet> loadSkinSet(const std::filesystem::path& skin);

/// Skin candidates in `dir`: regular `.wsz`/`.zip` files, case-insensitively,
/// hidden entries skipped, sorted, not recursive. A missing folder yields an empty
/// list rather than an error — a menu simply has fewer entries to offer.
std::vector<std::filesystem::path> listSkins(const std::filesystem::path& dir);

/// Assembles a `SkinSet` from an already-read container. Exposed so tests and
/// the loader can exercise the fallback logic without touching the filesystem.
std::optional<SkinSet> assembleSkinSet(const SkinContainer& container,
                                       const std::filesystem::path& source);

}  // namespace koalaamp::skins
