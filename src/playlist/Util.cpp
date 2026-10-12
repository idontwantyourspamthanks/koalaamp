// SPDX-License-Identifier: MIT
#include "playlist/Util.hpp"

#include <cstdio>
#include <fstream>
#include <system_error>

namespace koalaamp::playlist {
namespace {

/// Decodes one BMP scalar into UTF-8 (only used for the Latin-1 fallback, but
/// also handy for numeric XML entities).
void appendUtf8(std::string& out, unsigned int cp) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

}  // namespace

std::optional<std::string> readFileBytes(const std::filesystem::path& file, std::size_t maxBytes) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (!ec && size > maxBytes) {
        return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string data;
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    in.seekg(0, std::ios::beg);
    if (end > 0 && static_cast<std::size_t>(end) <= maxBytes) {
        data.resize(static_cast<std::size_t>(end));
        in.read(data.data(), static_cast<std::streamsize>(data.size()));
        data.resize(static_cast<std::size_t>(in.gcount()));
    } else {
        // Streamed fallback with a hard bound (unknown size / pipes).
        char buffer[65536];
        while (in && data.size() < maxBytes) {
            in.read(buffer, sizeof(buffer));
            data.append(buffer, static_cast<std::size_t>(in.gcount()));
        }
        if (data.size() > maxBytes) {
            return std::nullopt;
        }
    }
    if (!in && !in.eof()) {
        return std::nullopt;
    }
    return data;
}

bool writeFileAtomic(const std::filesystem::path& file, std::string_view bytes) {
    std::error_code ec;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), ec);
    }
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            std::error_code rm;
            std::filesystem::remove(tmp, rm);
            return false;
        }
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::error_code rm;
        std::filesystem::remove(tmp, rm);
        return false;
    }
    return true;
}

bool isValidUtf8(std::string_view s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char c = p[i];
        if (c < 0x80u) {
            ++i;
            continue;
        }
        int extra = 0;
        unsigned int cp = 0;
        if ((c & 0xE0u) == 0xC0u) {
            extra = 1; cp = c & 0x1Fu;
            if (cp == 0) return false;  // overlong
        } else if ((c & 0xF0u) == 0xE0u) {
            extra = 2; cp = c & 0x0Fu;
        } else if ((c & 0xF8u) == 0xF0u) {
            extra = 3; cp = c & 0x07u;
        } else {
            return false;
        }
        if (i + static_cast<std::size_t>(extra) >= n) {
            return false;
        }
        for (int k = 1; k <= extra; ++k) {
            const unsigned char cc = p[i + static_cast<std::size_t>(k)];
            if ((cc & 0xC0u) != 0x80u) {
                return false;
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (extra == 2 && cp < 0x800u) return false;
        if (extra == 3 && cp < 0x10000u) return false;
        if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
            return false;
        }
        i += static_cast<std::size_t>(extra) + 1;
    }
    return true;
}

std::string toValidUtf8(std::string_view bytes) {
    if (isValidUtf8(bytes)) {
        return std::string(bytes);
    }
    std::string out;
    out.reserve(bytes.size() + bytes.size() / 2);
    for (const char ch : bytes) {
        appendUtf8(out, static_cast<unsigned char>(ch));
    }
    return out;
}

std::string_view stripBom(std::string_view bytes) {
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEFu &&
        static_cast<unsigned char>(bytes[1]) == 0xBBu &&
        static_cast<unsigned char>(bytes[2]) == 0xBFu) {
        return bytes.substr(3);
    }
    return bytes;
}

std::string_view trim(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; };
    while (b < e && space(s[b])) ++b;
    while (e > b && space(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::string toLowerAscii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    }
    return out;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t nl = text.find('\n', start);
        std::string_view line = nl == std::string_view::npos ? text.substr(start)
                                                             : text.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        if (nl == std::string_view::npos) {
            break;
        }
        start = nl + 1;
    }
    return lines;
}

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

bool parseLong(std::string_view s, long& out) {
    s = trim(s);
    if (s.empty()) {
        return false;
    }
    std::size_t i = 0;
    bool negative = false;
    if (s[0] == '+' || s[0] == '-') {
        negative = s[0] == '-';
        i = 1;
    }
    if (i >= s.size()) {
        return false;
    }
    long value = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') {
            return false;
        }
        const long digit = c - '0';
        value = value * 10 + digit;
        if (value > 2147483647L) {
            return false;  // beyond the range any playlist field needs
        }
    }
    out = negative ? -value : value;
    return true;
}

std::string_view filePart(std::string_view path) {
    std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string_view::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

std::string xmlEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string fileUrlFor(std::string_view filename) {
    // A reference that already has a scheme (http://, file://, …) is left as-is.
    if (filename.find("://") != std::string_view::npos) {
        return std::string(filename);
    }
    std::string out = "file://";
    out += filename;
    return out;
}

std::string_view stripFileScheme(std::string_view ref) {
    if (ref.size() >= 7 && equalsIgnoreCase(ref.substr(0, 7), "file://")) {
        return ref.substr(7);
    }
    if (ref.size() >= 5 && equalsIgnoreCase(ref.substr(0, 5), "file:")) {
        return ref.substr(5);
    }
    return ref;
}

}  // namespace koalaamp::playlist
