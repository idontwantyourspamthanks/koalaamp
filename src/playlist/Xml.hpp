// SPDX-License-Identifier: MIT
//
// Xml — a tiny, tolerant, exception-free XML reader for the playlist formats
// (B4S, XSPF, ASX, WPL, playlists.xml). The playlist module is Qt-free (like
// src/audio) so QXmlStreamReader is not available; a full DOM is overkill and
// a strict parser would defeat the leniency the ticket demands. This scanner
// extracts elements/attributes/text and skips everything it does not
// understand (comments, PIs, DOCTYPE, CDATA) while always advancing, so fuzz
// input cannot hang or overrun it.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace koalaamp::playlist::xml {

struct Attr {
    std::string name;   ///< raw, as written (namespace `:` preserved)
    std::string value;  ///< entity-decoded
};

struct Node {
    std::string name;
    std::vector<Attr> attrs;
    std::string text{};  ///< concatenated direct text, decoded, untrimmed
    /// Owned children (unique_ptr so the tree's destructor recurses only to the
    /// (capped) element depth, never input depth).
    std::vector<std::unique_ptr<Node>> children;
};

/// Parses the first element of `input`. Never throws; malformed input yields
/// whatever prefix could be understood (an empty-name root means "no element").
Node parse(std::string_view input);

/// Case-insensitive attribute lookup, or nullptr.
const std::string* attribute(const Node& node, std::string_view name);
/// Case-insensitive first-child lookup, or nullptr.
const Node* child(const Node& node, std::string_view name);
/// First descendant (depth-first, self excluded) whose name matches.
const Node* descendant(const Node& node, std::string_view name);

/// Entity-decodes `s` (`&amp;` etc.); unknown entities pass through literally.
std::string unescape(std::string_view s);

}  // namespace koalaamp::playlist::xml
