// SPDX-License-Identifier: MIT
// library — Winamp smart-view query grammar. See Query.hpp.

#include "library/Query.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <ctime>
#include <map>
#include <string>
#include <utility>

namespace koalaamp::library {
namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool isWordChar(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_';
}

enum class FieldKind { Text, Number, Date };

struct Field {
    const char* column;
    FieldKind kind;
};

// Winamp field names (formats.md §5). `length` is seconds on the wire and
// milliseconds in the DB, so it is converted in parseComparison().
const Field* fieldFor(std::string_view name) {
    static const std::map<std::string_view, Field, std::less<>> kFields = {
        {"filename", {"path", FieldKind::Text}},
        {"path", {"path", FieldKind::Text}},
        {"title", {"title", FieldKind::Text}},
        {"artist", {"artist", FieldKind::Text}},
        {"album", {"album", FieldKind::Text}},
        {"albumartist", {"album_artist", FieldKind::Text}},
        {"album_artist", {"album_artist", FieldKind::Text}},
        {"genre", {"genre", FieldKind::Text}},
        {"comment", {"comment", FieldKind::Text}},
        {"composer", {"composer", FieldKind::Text}},
        {"publisher", {"publisher", FieldKind::Text}},
        {"year", {"year", FieldKind::Number}},
        {"trackno", {"trackno", FieldKind::Number}},
        {"disc", {"discno", FieldKind::Number}},
        {"discno", {"discno", FieldKind::Number}},
        {"length", {"duration_ms", FieldKind::Number}},
        {"bitrate", {"bitrate", FieldKind::Number}},
        {"rating", {"rating", FieldKind::Number}},
        {"playcount", {"playcount", FieldKind::Number}},
        {"bpm", {"bpm", FieldKind::Number}},
        {"lastplay", {"lastplayed", FieldKind::Date}},
        {"lastplayed", {"lastplayed", FieldKind::Date}},
        {"dateadded", {"dateadded", FieldKind::Date}},
        {"lastupd", {"mtime", FieldKind::Date}},
    };
    const auto it = kFields.find(name);
    return it == kFields.end() ? nullptr : &it->second;
}

std::int64_t relativeSeconds(std::string_view text) {
    // "[1 week ago]" / "[2 days ago]".
    std::size_t start = text.find_first_not_of(" \t");
    if (start == std::string_view::npos) {
        return 0;
    }
    std::size_t end = start;
    while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
        ++end;
    }
    std::int64_t count = 0;
    const auto [ptr, ec] = std::from_chars(text.data() + start, text.data() + end, count);
    if (ec != std::errc{}) {
        return 0;
    }
    std::size_t unitStart = end;
    while (unitStart < text.size() && std::isspace(static_cast<unsigned char>(text[unitStart]))) {
        ++unitStart;
    }
    std::size_t unitEnd = unitStart;
    while (unitEnd < text.size() && std::isalpha(static_cast<unsigned char>(text[unitEnd]))) {
        ++unitEnd;
    }
    const std::string unit = lower(text.substr(unitStart, unitEnd - unitStart));
    constexpr std::int64_t kMinute = 60;
    constexpr std::int64_t kHour = 60 * kMinute;
    constexpr std::int64_t kDay = 24 * kHour;
    std::int64_t scale = 0;
    if (unit.rfind("minute", 0) == 0) {
        scale = kMinute;
    } else if (unit.rfind("hour", 0) == 0) {
        scale = kHour;
    } else if (unit.rfind("day", 0) == 0) {
        scale = kDay;
    } else if (unit.rfind("week", 0) == 0) {
        scale = 7 * kDay;
    } else if (unit.rfind("month", 0) == 0) {
        scale = 30 * kDay;
    } else if (unit.rfind("year", 0) == 0) {
        scale = 365 * kDay;
    }
    return scale == 0 ? 0 : count * scale;
}

class Parser {
public:
    Parser(std::string_view text, std::int64_t nowUnix) : text_(text), now_(nowUnix) {}

    QueryParseResult run() {
        skipSpace();
        if (pos_ >= text_.size()) {
            ParsedQuery query;
            query.where = "1";
            return QueryParseResult{std::move(query), {}, 0};
        }
        std::string expression = parseOr();
        skipSpace();
        if (error_.empty() && pos_ < text_.size()) {
            fail("unexpected trailing input");
        }
        if (!error_.empty()) {
            return QueryParseResult{std::nullopt, error_, errorPos_};
        }
        ParsedQuery query;
        query.where = std::move(expression);
        query.params = std::move(params_);
        return QueryParseResult{std::move(query), {}, 0};
    }

private:
    void skipSpace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    bool match(std::string_view token) {
        skipSpace();
        if (text_.substr(pos_, token.size()) != token) {
            return false;
        }
        pos_ += token.size();
        return true;
    }

    bool matchWord(std::string_view word) {
        skipSpace();
        if (text_.size() - pos_ < word.size()) {
            return false;
        }
        for (std::size_t i = 0; i < word.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(text_[pos_ + i])) != word[i]) {
                return false;
            }
        }
        if (pos_ + word.size() < text_.size() &&
            isWordChar(static_cast<unsigned char>(text_[pos_ + word.size()]))) {
            return false;
        }
        pos_ += word.size();
        return true;
    }

    void fail(std::string message) {
        if (error_.empty()) {
            error_ = std::move(message);
            errorPos_ = pos_;
        }
    }

    std::string parseOr() {
        std::string left = parseAnd();
        while (error_.empty()) {
            skipSpace();
            if (!(match("||") || matchWord("or"))) {
                break;
            }
            std::string right = parseAnd();
            left = "(" + left + " OR " + right + ")";
        }
        return left;
    }

    std::string parseAnd() {
        std::string left = parseUnary();
        while (error_.empty()) {
            skipSpace();
            if (!(match("&&") || matchWord("and"))) {
                break;
            }
            std::string right = parseUnary();
            left = "(" + left + " AND " + right + ")";
        }
        return left;
    }

    std::string parseUnary() {
        skipSpace();
        // "1" is the internal "match everything" sentinel used when a filter is
        // combined with the All-tracks browse state.
        if (matchWord("1")) {
            return "1";
        }
        if (match("!")) {
            return "(NOT " + parseUnary() + ")";
        }
        if (match("(")) {
            std::string inner = parseOr();
            if (!match(")")) {
                fail("expected ')'");
            }
            return "(" + inner + ")";
        }
        return parseComparison();
    }

    std::string parseComparison() {
        skipSpace();
        const std::size_t fieldStart = pos_;
        while (pos_ < text_.size() && (isWordChar(static_cast<unsigned char>(text_[pos_])))) {
            ++pos_;
        }
        if (fieldStart == pos_) {
            fail("expected a field name");
            return {};
        }
        const std::string fieldName = lower(text_.substr(fieldStart, pos_ - fieldStart));
        const Field* field = fieldFor(fieldName);
        if (field == nullptr) {
            fail("unknown field '" + fieldName + "'");
            return {};
        }

        skipSpace();
        if (matchWord("isempty")) {
            if (field->kind == FieldKind::Text) {
                return "(" + std::string(field->column) + " IS NULL OR " + field->column + " = '')";
            }
            return "(" + std::string(field->column) + " IS NULL OR " + field->column + " = 0)";
        }

        std::string op;
        if (match("!=")) {
            op = "!=";
        } else if (match("<=")) {
            op = "<=";
        } else if (match(">=")) {
            op = ">=";
        } else if (match("=")) {
            op = "=";
        } else if (match("<")) {
            op = "<";
        } else if (match(">")) {
            op = ">";
        } else if (matchWord("has")) {
            op = "has";
        } else if (matchWord("not has") || matchWord("nothas")) {
            op = "nothas";
        } else if (matchWord("like")) {
            op = "like";
        } else if (matchWord("begins")) {
            op = "begins";
        } else if (matchWord("ends")) {
            op = "ends";
        } else {
            fail("expected an operator");
            return {};
        }

        QueryParam param;
        if (field->kind == FieldKind::Text) {
            param.type = QueryParam::Type::Text;
            if (!readTextValue(param.text)) {
                return {};
            }
        } else {
            const std::string raw = readNumberValue();
            if (!error_.empty()) {
                return {};
            }
            param.type = QueryParam::Type::Integer;
            std::int64_t value = 0;
            const auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), value);
            if (ec != std::errc{}) {
                fail("expected a numeric value");
                return {};
            }
            if (fieldName == "length") {
                value *= 1000; // Winamp queries use seconds
            }
            param.integer = value;
        }

        const std::string column = field->column;
        std::string sql;
        if (field->kind == FieldKind::Text) {
            if (op == "has" || op == "like" || op == "nothas") {
                param.text = "%" + param.text + "%";
                sql = column + (op == "nothas" ? " NOT LIKE ?" : " LIKE ?");
                sql += " COLLATE NOCASE";
            } else if (op == "begins") {
                param.text += "%";
                sql = column + " LIKE ? COLLATE NOCASE";
            } else if (op == "ends") {
                param.text = "%" + param.text;
                sql = column + " LIKE ? COLLATE NOCASE";
            } else if (op == "!=") {
                sql = column + " <> ? COLLATE NOCASE";
            } else if (op == "=") {
                sql = column + " = ? COLLATE NOCASE";
            } else {
                sql = column + " " + op + " ?";
            }
        } else {
            sql = column + " " + op + " ?";
        }
        params_.push_back(std::move(param));
        return "(" + sql + ")";
    }

    bool readTextValue(std::string& out) {
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == '[') {
            const std::size_t close = text_.find(']', pos_ + 1);
            if (close == std::string_view::npos) {
                fail("unterminated relative date");
                return false;
            }
            const std::string_view inside = text_.substr(pos_ + 1, close - pos_ - 1);
            const std::int64_t seconds = relativeSeconds(inside);
            if (seconds == 0) {
                fail("invalid relative date");
                return false;
            }
            out = std::to_string(now_ - seconds);
            pos_ = close + 1;
            return true;
        }
        if (pos_ < text_.size() && (text_[pos_] == '"' || text_[pos_] == '\'')) {
            const char quote = text_[pos_++];
            const std::size_t start = pos_;
            while (pos_ < text_.size() && text_[pos_] != quote) {
                ++pos_;
            }
            if (pos_ >= text_.size()) {
                fail("unterminated quoted string");
                return false;
            }
            out = std::string(text_.substr(start, pos_ - start));
            ++pos_;
            return true;
        }
        const std::size_t start = pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (std::isspace(static_cast<unsigned char>(c)) || c == ')' ||
                text_.substr(pos_, 2) == "&&" || text_.substr(pos_, 2) == "||") {
                break;
            }
            ++pos_;
        }
        if (start == pos_) {
            fail("expected a value");
            return false;
        }
        out = std::string(text_.substr(start, pos_ - start));
        return true;
    }

    std::string readNumberValue() {
        std::string value;
        if (!readTextValue(value)) {
            return {};
        }
        return value;
    }

    std::string_view text_;
    std::size_t pos_{0};
    std::int64_t now_{0};
    std::vector<QueryParam> params_;
    std::string error_;
    std::size_t errorPos_{0};
};

} // namespace

QueryParseResult parseQuery(std::string_view text, std::int64_t nowUnix) {
    return Parser(text, nowUnix).run();
}

} // namespace koalaamp::library
