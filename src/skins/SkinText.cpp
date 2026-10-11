// SPDX-License-Identifier: MIT
#include "skins/SkinText.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace koalaamp::skins {
namespace {

bool isSpaceChar(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string trim(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && isSpaceChar(s[b])) {
        ++b;
    }
    while (e > b && isSpaceChar(s[e - 1])) {
        --e;
    }
    return std::string(s.substr(b, e - b));
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// Splits a string on separators and returns the non-empty trimmed tokens.
std::vector<std::string> tokenize(std::string_view s, std::string_view separators) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t end = s.find_first_of(separators, start);
        if (end == std::string_view::npos) {
            end = s.size();
        }
        std::string tok = trim(s.substr(start, end - start));
        if (!tok.empty()) {
            out.push_back(std::move(tok));
        }
        if (end == s.size()) {
            break;
        }
        start = end + 1;
    }
    return out;
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        if (end == text.size()) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

/// Whole-line comment in the styles real skins use (`;`, `#`, `//`).
bool isCommentLine(std::string_view trimmed) {
    if (trimmed.empty()) {
        return false;
    }
    if (trimmed[0] == ';' || trimmed[0] == '#') {
        return true;
    }
    return trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/';
}

int clamp255(long long v) {
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return static_cast<int>(v);
}

std::optional<long long> parseInt(std::string_view tok) {
    if (tok.empty()) {
        return std::nullopt;
    }
    long long value = 0;
    const auto* first = tok.data();
    const auto* last = tok.data() + tok.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc{} || ptr != last) {
        return std::nullopt;
    }
    return value;
}

std::optional<long long> parseHex(std::string_view tok) {
    if (tok.empty()) {
        return std::nullopt;
    }
    long long value = 0;
    const auto* first = tok.data();
    const auto* last = tok.data() + tok.size();
    const auto [ptr, ec] = std::from_chars(first, last, value, 16);
    if (ec != std::errc{} || ptr != last) {
        return std::nullopt;
    }
    return value;
}

std::string_view stripColorPrefix(std::string_view tok) {
    if (tok.size() >= 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
        return tok.substr(2);
    }
    if (!tok.empty() && tok[0] == '#') {
        return tok.substr(1);
    }
    return tok;
}

bool isHexDigits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
               return std::isxdigit(c) != 0;
           });
}

}  // namespace

std::optional<int> parseColorComponent(std::string_view token) {
    const std::string tok = trim(token);
    if (tok.empty()) {
        return std::nullopt;
    }
    const std::string_view view(tok);
    const bool prefixed = view[0] == '#' || (view.size() >= 2 && view[0] == '0' &&
                                              (view[1] == 'x' || view[1] == 'X'));
    const std::string_view body = stripColorPrefix(view);
    std::optional<long long> value;
    if (prefixed) {
        if (isHexDigits(body)) {
            value = parseHex(body);
        }
    } else if (std::all_of(view.begin(), view.end(),
                           [](unsigned char c) { return std::isdigit(c) != 0; })) {
        value = parseInt(view);
    } else if (isHexDigits(view)) {
        value = parseHex(view);
    }
    if (!value) {
        return std::nullopt;
    }
    return clamp255(*value);
}

std::optional<Rgb> parseRgbValue(std::string_view value) {
    std::string v = trim(value);
    // Strip one layer of surrounding quotes (Webamp does this in parseIni too).
    if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') ||
                          (v.front() == '\'' && v.back() == '\''))) {
        v = v.substr(1, v.size() - 2);
        v = trim(v);
    }
    if (v.empty()) {
        return std::nullopt;
    }

    if (v.find(',') != std::string::npos || v.find(' ') != std::string::npos ||
        v.find('\t') != std::string::npos) {
        const auto tokens = tokenize(v, ", \t");
        if (tokens.size() < 3) {
            return std::nullopt;
        }
        std::optional<int> comps[3];
        for (std::size_t i = 0; i < 3; ++i) {
            comps[i] = parseColorComponent(tokens[i]);
            if (!comps[i]) {
                return std::nullopt;
            }
        }
        return Rgb{*comps[0], *comps[1], *comps[2]};
    }

    // Hex form. Winamp tolerates over-long values, so truncate to six digits;
    // short values are left-padded (a bare number, not CSS shorthand).
    std::string_view body = stripColorPrefix(v);
    if (!isHexDigits(body)) {
        return std::nullopt;
    }
    std::string hex(body.substr(0, std::min<std::size_t>(body.size(), 6)));
    while (hex.size() < 6) {
        hex.insert(hex.begin(), '0');
    }
    const auto num = parseHex(hex);
    if (!num) {
        return std::nullopt;
    }
    return Rgb{static_cast<int>(((*num) >> 16) & 0xFF), static_cast<int>(((*num) >> 8) & 0xFF),
               static_cast<int>((*num) & 0xFF)};
}

IniSections parseIni(std::string_view text) {
    IniSections data;
    std::string section;
    bool haveSection = false;
    for (std::string_view raw : splitLines(text)) {
        const std::string line = trim(raw);
        if (line.empty() || isCommentLine(line)) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']' && line.size() >= 2) {
            section = lower(trim(std::string_view(line).substr(1, line.size() - 2)));
            haveSection = !section.empty();
            if (haveSection) {
                data.try_emplace(section);
            }
            continue;
        }
        if (!haveSection) {
            continue;  // properties before the first section are ignored (Webamp)
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) {
            continue;
        }
        const std::string key = lower(trim(std::string_view(line).substr(0, eq)));
        if (key.empty()) {
            continue;
        }
        std::string value = trim(std::string_view(line).substr(eq + 1));
        // Ignore anything after a second `=` (Webamp).
        const auto second = value.find('=');
        if (second != std::string::npos) {
            value = trim(std::string_view(value).substr(0, second));
        }
        // Strip one layer of surrounding quotes.
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                  (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        data[section][key] = value;
    }
    return data;
}

VisColor defaultVisColor() {
    VisColor vc;
    vc.colors = {{{0, 0, 0}, {24, 33, 41}, {239, 49, 16}, {206, 41, 16}, {214, 90, 0},
                  {214, 102, 0}, {214, 115, 0}, {198, 123, 8}, {222, 165, 24}, {214, 181, 33},
                  {189, 222, 41}, {148, 222, 33}, {41, 206, 16}, {50, 190, 16}, {57, 181, 16},
                  {49, 156, 8}, {41, 148, 0}, {24, 132, 8}, {255, 255, 255}, {214, 214, 222},
                  {181, 189, 189}, {160, 170, 175}, {148, 156, 165}, {150, 150, 150}}};
    return vc;
}

VisColor parseVisColor(std::string_view text) {
    VisColor vc = defaultVisColor();
    std::size_t index = 0;
    for (std::string_view raw : splitLines(text)) {
        if (index >= VisColor::kCount) {
            break;
        }
        // Drop a trailing `//` comment before tokenizing.
        if (const auto slash = raw.find("//"); slash != std::string_view::npos) {
            raw = raw.substr(0, slash);
        }
        const std::string line = trim(raw);
        if (line.empty() || isCommentLine(line)) {
            continue;  // skipped lines do not consume a colour slot (Webamp)
        }
        const auto tokens = tokenize(line, ", \t");
        if (tokens.size() < 3) {
            continue;
        }
        std::optional<int> comps[3];
        bool ok = true;
        for (std::size_t i = 0; i < 3; ++i) {
            comps[i] = parseColorComponent(tokens[i]);
            ok = ok && comps[i].has_value();
        }
        if (!ok) {
            continue;
        }
        vc.colors[index++] = Rgb{*comps[0], *comps[1], *comps[2]};
    }
    return vc;
}

PleditColors defaultPleditColors() { return PleditColors{}; }

PleditColors parsePledit(std::string_view text) {
    PleditColors colors;
    const IniSections ini = parseIni(text);
    const auto section = ini.find("text");
    if (section == ini.end()) {
        return colors;  // corrupt or missing [Text]: Webamp uses the defaults
    }
    const auto& keys = section->second;
    const auto assign = [&keys](const char* key, Rgb& target) {
        const auto it = keys.find(key);
        if (it == keys.end()) {
            return;
        }
        if (const auto rgb = parseRgbValue(it->second)) {
            target = *rgb;
        }
    };
    assign("normal", colors.normal);
    assign("current", colors.current);
    assign("normalbg", colors.normalBg);
    assign("selectedbg", colors.selectedBg);
    assign("mbfg", colors.mbFg);
    assign("mbbg", colors.mbBg);
    if (const auto it = keys.find("font"); it != keys.end() && !it->second.empty()) {
        colors.font = it->second;
    }
    return colors;
}

std::size_t Region::polygonCount() const {
    std::size_t n = 0;
    for (const auto& [name, polygons] : sections) {
        n += polygons.size();
    }
    return n;
}

std::size_t Region::pointCount() const {
    std::size_t n = 0;
    for (const auto& [name, polygons] : sections) {
        for (const auto& polygon : polygons) {
            n += polygon.points.size();
        }
    }
    return n;
}

Region parseRegion(std::string_view text) {
    // Cap the geometry we retain from a hostile file (fuzz target).
    constexpr std::size_t kMaxPoints = 100000;
    Region region;
    const IniSections ini = parseIni(text);
    for (const auto& [sectionName, keys] : ini) {
        const auto listIt = keys.find("pointlist");
        if (listIt == keys.end()) {
            continue;
        }
        const auto flat = tokenize(listIt->second, ", \t");
        std::vector<RegionPoint> points;
        int x = 0;
        bool haveX = false;
        for (const auto& tok : flat) {
            const auto value = parseInt(tok);
            if (!value) {
                continue;  // tolerate junk tokens between coordinates
            }
            if (!haveX) {
                x = static_cast<int>(*value);
                haveX = true;
            } else {
                points.push_back(RegionPoint{x, static_cast<int>(*value)});
                haveX = false;
                if (points.size() >= kMaxPoints) {
                    break;
                }
            }
        }
        if (points.size() < 3) {
            continue;  // no polygon can be formed
        }

        std::vector<std::size_t> counts;
        if (const auto numIt = keys.find("numpoints"); numIt != keys.end()) {
            for (const auto& tok : tokenize(numIt->second, ", \t")) {
                if (const auto value = parseInt(tok); value && *value >= 0) {
                    counts.push_back(static_cast<std::size_t>(*value));
                }
            }
        }
        if (counts.empty()) {
            // More forgiving than Winamp (which crashed) and Webamp (which drops
            // the section): treat the whole point list as one polygon.
            counts.push_back(points.size());
        }

        std::vector<RegionPolygon> polygons;
        std::size_t index = 0;
        for (const std::size_t count : counts) {
            if (index >= points.size()) {
                break;
            }
            const std::size_t take = std::min(count, points.size() - index);
            if (count >= 3 && take >= 3) {
                RegionPolygon polygon;
                polygon.points.assign(points.begin() + static_cast<std::ptrdiff_t>(index),
                                      points.begin() + static_cast<std::ptrdiff_t>(index + take));
                polygons.push_back(std::move(polygon));
            }
            index += count;
        }
        // Mismatched NumPoints/PointList: keep any trailing vertices as a final
        // polygon rather than silently discarding them.
        if (index < points.size() && points.size() - index >= 3) {
            RegionPolygon polygon;
            polygon.points.assign(points.begin() + static_cast<std::ptrdiff_t>(index), points.end());
            polygons.push_back(std::move(polygon));
        }
        if (!polygons.empty()) {
            region.sections[sectionName] = std::move(polygons);
        }
    }
    return region;
}

}  // namespace koalaamp::skins
