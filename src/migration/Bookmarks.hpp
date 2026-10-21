// SPDX-License-Identifier: MIT
// migration — Winamp bookmark reader (MI-1).
//
// `winamp.bm` is a one-way import: a UTF-8/UTF-16 text list, one bookmark per
// line. The canonical line is `title|filename`; a bare filename is accepted and
// gets the file-stem title. Comments, blank lines and the file's optional header
// are ignored. No Winamp file is ever written.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace koalaamp::migration {

struct Bookmark {
    std::string title;
    std::string filename;
};

struct BookmarkFile {
    std::vector<Bookmark> bookmarks;
    std::size_t skipped{0}; ///< non-empty lines without a usable filename
    std::string error;      ///< unreadable/binary input; empty on success
    bool ok() const { return error.empty(); }
};

/// Parses the file's bytes (BOM-aware UTF-8/UTF-16). Public for fixture tests.
BookmarkFile parseBookmarks(std::string_view bytes);

/// Reads `winamp.bm`/`winamp.bm8`. Missing/unreadable files are errors.
BookmarkFile readBookmarks(const std::filesystem::path& file);

} // namespace koalaamp::migration
