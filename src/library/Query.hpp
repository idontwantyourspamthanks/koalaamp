// SPDX-License-Identifier: MIT
// library — Winamp smart-view query grammar -> SQL WHERE fragment (LI-3).
//
// The parser produces parameter placeholders, never interpolated user text.
// Supported fields: filename/title/artist/album/albumartist/year/genre/comment/
// trackno/disc/length/bitrate/rating/playcount/bpm/lastplay/dateadded/lastupd.
// Operators: = != < > <= >= HAS NOTHAS LIKE BEGINS ENDS ISEMPTY, with && || !
// and parentheses; `[1 week ago]` resolves against `nowUnix`.

#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace koalaamp::library {

struct QueryParam {
    enum class Type { Text, Integer };
    Type type{Type::Text};
    std::string text;
    std::int64_t integer{0};
};

struct ParsedQuery {
    /// SQL fragment for a WHERE clause; "1" means everything.
    std::string where;
    std::vector<QueryParam> params;
};

struct QueryParseResult {
    std::optional<ParsedQuery> query;
    std::string error;
    std::size_t errorPos{0};
};

/// Parses a Winamp query. `nowUnix` is injected for deterministic relative-date
/// tests; production passes the current time.
QueryParseResult parseQuery(std::string_view text,
                            std::int64_t nowUnix = static_cast<std::int64_t>(std::time(nullptr)));

} // namespace koalaamp::library
