// SPDX-License-Identifier: MIT
#include "ui/Atf.hpp"

#include <cstddef>
#include <vector>

namespace koalaamp::ui {

namespace {

/// Recursive-descent evaluator over the format string. `stop` characters end
/// the current sequence (')' for a function argument list, ']' for a block).
class Evaluator {
public:
    Evaluator(std::string_view text, const AtfFields& fields)
        : m_text(text), m_fields(fields) {}

    /// Evaluates until one of `stops` or the end of input.
    std::string sequence(std::string_view stops) {
        std::string out;
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (stops.find(c) != std::string_view::npos) {
                break;
            }
            if (c == '%') {
                out += variable();
            } else if (c == '[') {
                out += block();
            } else if (c == '$') {
                out += function();
            } else {
                out.push_back(c);
                ++m_pos;
            }
        }
        return out;
    }

private:
    std::string variable() {
        const std::size_t close = m_text.find('%', m_pos + 1);
        if (close == std::string_view::npos) {
            ++m_pos;  // lone '%' is literal
            return "%";
        }
        const std::string name(m_text.substr(m_pos + 1, close - m_pos - 1));
        m_pos = close + 1;
        std::string value;
        if (name == "artist") value = m_fields.artist;
        else if (name == "title") value = m_fields.title;
        else if (name == "album") value = m_fields.album;
        else if (name == "filename") value = m_fields.filename;
        ++m_fieldCount;
        if (value.empty()) {
            ++m_emptyFields;
        }
        return value;
    }

    /// `[...]` — keep the rendered block only when it has no empty field.
    /// Winamp shows bracketed text only if every field inside is present.
    std::string block() {
        ++m_pos;  // consume '['
        const std::size_t fieldsBefore = m_fieldCount;
        const std::size_t emptiesBefore = m_emptyFields;
        const std::string inner = sequence("]");
        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            ++m_pos;
        }
        const bool hasField = m_fieldCount > fieldsBefore;
        const bool hasEmpty = m_emptyFields > emptiesBefore;
        return (hasField && hasEmpty) ? std::string{} : inner;
    }

    std::string function() {
        // $name( ... )
        const std::size_t nameEnd = m_text.find('(', m_pos + 1);
        if (nameEnd == std::string_view::npos) {
            ++m_pos;
            return "$";
        }
        const std::string name(m_text.substr(m_pos + 1, nameEnd - m_pos - 1));
        m_pos = nameEnd + 1;
        const std::vector<std::string> args = arguments();
        if (m_pos < m_text.size() && m_text[m_pos] == ')') {
            ++m_pos;
        }
        std::string result;
        if (name == "if2") {
            for (const std::string& arg : args) {
                if (!arg.empty()) {
                    result = arg;
                    break;
                }
            }
        } else if (name == "filepart") {
            result = args.empty() ? std::string{} : filePart(args.front());
        } else {
            // Unknown function: pass its first argument (Winamp's permissive
            // behaviour for functions it does not know).
            result = args.empty() ? std::string{} : args.front();
        }
        ++m_fieldCount;
        if (result.empty()) {
            ++m_emptyFields;
        }
        return result;
    }

    /// Evaluates comma-separated top-level arguments up to the matching ')'.
    std::vector<std::string> arguments() {
        std::vector<std::string> args;
        std::string current;
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == ')' ) {
                break;
            }
            if (c == ',') {
                args.push_back(current);
                current.clear();
                ++m_pos;
                continue;
            }
            if (c == '%') {
                current += variable();
            } else if (c == '[') {
                current += block();
            } else if (c == '$') {
                current += function();
            } else {
                current.push_back(c);
                ++m_pos;
            }
        }
        args.push_back(current);
        return args;
    }

    std::string_view m_text;
    const AtfFields& m_fields;
    std::size_t m_pos = 0;
    std::size_t m_fieldCount = 0;
    std::size_t m_emptyFields = 0;
};

}  // namespace

std::string filePart(std::string_view path) {
    // Drop any query/fragment so URLs behave like paths.
    const std::size_t cut = path.find_first_of("?#");
    if (cut != std::string_view::npos) {
        path = path.substr(0, cut);
    }
    const std::size_t slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

std::string formatAtf(std::string_view format, const AtfFields& fields) {
    Evaluator evaluator(format, fields);
    return evaluator.sequence({});
}

AtfFields atfFieldsFor(const playback::MediaRef& media) {
    AtfFields fields;
    fields.filename = media.value;
    const std::string name = filePart(media.value);
    const std::size_t dot = name.find_last_of('.');
    fields.title = dot == std::string::npos ? name : name.substr(0, dot);
    return fields;
}

std::string defaultPlaylistTitle(const playback::MediaRef& media) {
    return formatAtf(kDefaultAtf, atfFieldsFor(media));
}

}  // namespace koalaamp::ui
