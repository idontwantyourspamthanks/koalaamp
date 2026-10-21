// SPDX-License-Identifier: MIT
// migration — Winamp bookmark reader. See Bookmarks.hpp.

#include "migration/Bookmarks.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>

namespace koalaamp::migration {
namespace {

void appendUtf8(std::string& out, std::uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

std::string utf16ToUtf8(const unsigned char* data, std::size_t bytes, bool littleEndian) {
    std::string out;
    out.reserve(bytes);
    const auto unit = [&](std::size_t offset) -> std::uint32_t {
        return littleEndian ? static_cast<std::uint32_t>(data[offset]) |
                                  (static_cast<std::uint32_t>(data[offset + 1]) << 8)
                            : (static_cast<std::uint32_t>(data[offset]) << 8) |
                                  static_cast<std::uint32_t>(data[offset + 1]);
    };
    for (std::size_t i = 0; i + 1 < bytes; i += 2) {
        std::uint32_t codepoint = unit(i);
        if (codepoint >= 0xd800 && codepoint <= 0xdbff && i + 3 < bytes) {
            const std::uint32_t low = unit(i + 2);
            if (low >= 0xdc00 && low <= 0xdfff) {
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                i += 2;
            }
        }
        if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
            codepoint = 0xfffd;
        }
        appendUtf8(out, codepoint);
    }
    return out;
}

bool decodeText(std::string_view bytes, std::string& text) {
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    if (bytes.size() >= 2 && data[0] == 0xff && data[1] == 0xfe) {
        text = utf16ToUtf8(data + 2, bytes.size() - 2, true);
    } else if (bytes.size() >= 2 && data[0] == 0xfe && data[1] == 0xff) {
        text = utf16ToUtf8(data + 2, bytes.size() - 2, false);
    } else {
        std::string_view raw = bytes;
        if (raw.size() >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf) {
            raw.remove_prefix(3);
        }
        text.assign(raw);
    }
    // A NUL outside a UTF-16 BOM means this is not the text bookmark file.
    return text.find('\0') == std::string::npos;
}

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return ch != ' ' && ch != '\t' && ch != '\r'; };
    const auto first = std::find_if(value.begin(), value.end(), notSpace);
    const auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return first < last ? std::string(first, last) : std::string{};
}

bool looksLikePath(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    if (value.rfind("//", 0) == 0 || value.rfind("\\\\", 0) == 0 ||
        value.find("://") != std::string::npos || value.find('\\') != std::string::npos) {
        return true;
    }
    if (value.front() == '/' || value.front() == '.') {
        return true;
    }
    return value.size() >= 2 && value[1] == ':' &&
           ((value[0] >= 'A' && value[0] <= 'Z') || (value[0] >= 'a' && value[0] <= 'z'));
}

std::string titleFromFilename(const std::string& filename) {
    const std::size_t slash = filename.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? filename : filename.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

} // namespace

BookmarkFile parseBookmarks(std::string_view bytes) {
    BookmarkFile file;
    std::string text;
    if (!decodeText(bytes, text)) {
        file.error = "bookmark file is not text";
        return file;
    }

    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string line =
            text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? text.size() + 1 : end + 1;
        line = trim(std::move(line));
        if (line.empty() || line.front() == '#' || line.front() == ';' || line.front() == '[') {
            continue;
        }
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (lower.rfind("winamp bookmark", 0) == 0) {
            continue;
        }

        // The first unescaped pipe separates title and filename. Windows paths
        // cannot contain '|', and the escape keeps titles/URLs representable.
        std::size_t pipe = std::string::npos;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '\\' && i + 1 < line.size()) {
                ++i;
                continue;
            }
            if (line[i] == '|') {
                pipe = i;
                break;
            }
        }
        if (pipe == std::string::npos) {
            Bookmark bookmark;
            bookmark.filename = line;
            bookmark.title = titleFromFilename(line);
            file.bookmarks.push_back(std::move(bookmark));
            continue;
        }

        std::string left = trim(line.substr(0, pipe));
        std::string right = trim(line.substr(pipe + 1));
        auto unescape = [](std::string value) {
            std::string out;
            for (std::size_t i = 0; i < value.size(); ++i) {
                if (value[i] == '\\' && i + 1 < value.size() &&
                    (value[i + 1] == '|' || value[i + 1] == '\\')) {
                    out.push_back(value[++i]);
                } else {
                    out.push_back(value[i]);
                }
            }
            return out;
        };
        left = unescape(std::move(left));
        right = unescape(std::move(right));
        Bookmark bookmark;
        if (looksLikePath(left) && !looksLikePath(right)) {
            bookmark.filename = left;
            bookmark.title = right;
        } else {
            bookmark.title = left;
            bookmark.filename = right;
        }
        if (bookmark.filename.empty()) {
            ++file.skipped;
            continue;
        }
        if (bookmark.title.empty()) {
            bookmark.title = titleFromFilename(bookmark.filename);
        }
        file.bookmarks.push_back(std::move(bookmark));
    }
    return file;
}

BookmarkFile readBookmarks(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        BookmarkFile result;
        result.error = "cannot read " + file.string();
        return result;
    }
    const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return parseBookmarks(bytes);
}

} // namespace koalaamp::migration
