// SPDX-License-Identifier: MIT
#include "playlist/Xml.hpp"

#include "playlist/Util.hpp"

#include <cstddef>

namespace koalaamp::playlist::xml {
namespace {

constexpr int kMaxDepth = 128;
constexpr std::size_t kMaxNodes = 200000;

bool isNameChar(char c) {
    return !(c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/' || c == '>' || c == '=' ||
             c == '<');
}

/// Reads an entity escape starting at `s[i] == '&'`. Returns true and advances
/// `i` past the `;` when recognised; otherwise leaves `i` put.
bool decodeEntity(std::string_view s, std::size_t& i, std::string& out) {
    const std::size_t semi = s.find(';', i + 1);
    if (semi == std::string_view::npos || semi - i > 12) {
        return false;
    }
    const std::string_view body = s.substr(i + 1, semi - i - 1);
    if (body == "amp") { out.push_back('&'); }
    else if (body == "lt") { out.push_back('<'); }
    else if (body == "gt") { out.push_back('>'); }
    else if (body == "quot") { out.push_back('"'); }
    else if (body == "apos") { out.push_back('\''); }
    else if (body.size() >= 2 && body[0] == '#') {
        unsigned int cp = 0;
        bool ok = true;
        if (body[1] == 'x' || body[1] == 'X') {
            if (body.size() < 3) return false;
            for (std::size_t k = 2; k < body.size(); ++k) {
                const char c = body[k];
                unsigned int d;
                if (c >= '0' && c <= '9') d = static_cast<unsigned int>(c - '0');
                else if (c >= 'a' && c <= 'f') d = static_cast<unsigned int>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') d = static_cast<unsigned int>(c - 'A' + 10);
                else { ok = false; break; }
                cp = cp * 16 + d;
                if (cp > 0x10FFFF) { ok = false; break; }
            }
        } else {
            for (std::size_t k = 1; k < body.size(); ++k) {
                const char c = body[k];
                if (c < '0' || c > '9') { ok = false; break; }
                cp = cp * 10 + static_cast<unsigned int>(c - '0');
                if (cp > 0x10FFFF) { ok = false; break; }
            }
        }
        if (!ok || cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        // Reuse the shared widening (Latin-1-style, but for a full scalar).
        if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    } else {
        return false;
    }
    i = semi + 1;
    return true;
}

void decodeInto(std::string& out, std::string_view raw) {
    std::size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] == '&') {
            if (decodeEntity(raw, i, out)) {
                continue;
            }
        }
        out.push_back(raw[i]);
        ++i;
    }
}

std::string decode(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    decodeInto(out, raw);
    return out;
}

void skipSpace(std::string_view s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
        ++i;
    }
}

/// Parses attributes from `i` (after the element name) until the tag closes.
/// Advances `i` to just past `>` (or to the end). `selfClosing` reports `/ >`.
void parseTag(std::string_view s, std::size_t& i, std::string& name, std::vector<Attr>& attrs,
              bool& selfClosing) {
    name.clear();
    while (i < s.size() && isNameChar(s[i])) {
        name.push_back(s[i]);
        ++i;
    }
    selfClosing = false;
    while (i < s.size()) {
        skipSpace(s, i);
        if (i >= s.size()) {
            break;
        }
        if (s[i] == '>') {
            ++i;
            return;
        }
        if (s[i] == '/') {
            selfClosing = true;
            ++i;
            continue;
        }
        if (s[i] == '<') {
            return;  // malformed; let the main loop resync
        }
        Attr attr;
        while (i < s.size() && isNameChar(s[i])) {
            attr.name.push_back(s[i]);
            ++i;
        }
        skipSpace(s, i);
        if (i < s.size() && s[i] == '=') {
            ++i;
            skipSpace(s, i);
            if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
                const char quote = s[i];
                ++i;
                const std::size_t start = i;
                while (i < s.size() && s[i] != quote) {
                    ++i;
                }
                attr.value = decode(s.substr(start, i - start));
                if (i < s.size()) {
                    ++i;
                }
            } else {
                const std::size_t start = i;
                while (i < s.size() && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' &&
                       s[i] != '\n' && s[i] != '>' && s[i] != '/') {
                    ++i;
                }
                attr.value = decode(s.substr(start, i - start));
            }
        }
        if (attr.name.empty()) {
            // Nothing consumed for a non-tag char: advance to guarantee progress.
            ++i;
        } else {
            attrs.push_back(std::move(attr));
        }
    }
}

}  // namespace

Node parse(std::string_view input) {
    Node root;
    // Iterative walk with an explicit stack of open element indices. Nesting is
    // capped (kMaxDepth) so neither a later recursive walk nor the tree's own
    // destructor can be driven into a stack overflow by a pathological file.
    std::vector<Node*> stack;
    std::size_t nodeCount = 0;
    std::size_t i = 0;
    Node* current = &root;
    bool sawRoot = false;
    int skipDepth = 0;  ///< >0 while inside a subtree deeper than kMaxDepth

    while (i < input.size()) {
        if (input[i] != '<') {
            const std::size_t start = i;
            while (i < input.size() && input[i] != '<') {
                ++i;
            }
            if (skipDepth == 0) {
                decodeInto(current->text, input.substr(start, i - start));
            }
            continue;
        }

        // CDATA
        if (input.compare(i, 9, "<![CDATA[") == 0) {
            const std::size_t start = i + 9;
            const std::size_t end = input.find("]]>", start);
            const std::size_t stop = end == std::string_view::npos ? input.size() : end;
            if (skipDepth == 0) {
                current->text.append(input.substr(start, stop - start));
            }
            i = end == std::string_view::npos ? input.size() : end + 3;
            continue;
        }
        // Comment
        if (input.compare(i, 4, "<!--") == 0) {
            const std::size_t end = input.find("-->", i + 4);
            i = end == std::string_view::npos ? input.size() : end + 3;
            continue;
        }
        // Processing instruction / XML declaration
        if (input.compare(i, 2, "<?") == 0) {
            const std::size_t end = input.find("?>", i + 2);
            i = end == std::string_view::npos ? input.size() : end + 2;
            continue;
        }
        // Declaration (DOCTYPE etc.)
        if (input.compare(i, 2, "<!") == 0) {
            const std::size_t end = input.find('>', i + 2);
            i = end == std::string_view::npos ? input.size() : end + 1;
            continue;
        }
        // End tag
        if (input[i] == '<' && i + 1 < input.size() && input[i + 1] == '/') {
            const std::size_t end = input.find('>', i + 2);
            i = end == std::string_view::npos ? input.size() : end + 1;
            if (skipDepth > 0) {
                --skipDepth;
            } else if (!stack.empty()) {
                stack.pop_back();
                current = stack.empty() ? &root : stack.back();
            }
            continue;
        }

        // Start tag
        ++i;  // consume '<'
        std::string name;
        std::vector<Attr> attrs;
        bool selfClosing = false;
        parseTag(input, i, name, attrs, selfClosing);
        if (name.empty()) {
            continue;
        }
        if (!sawRoot) {
            root.name = std::move(name);
            root.attrs = std::move(attrs);
            root.children.clear();
            root.text.clear();
            sawRoot = true;
            ++nodeCount;
            if (!selfClosing) {
                stack.clear();
                stack.push_back(&root);
                current = &root;
            } else {
                current = &root;
            }
            continue;
        }
        if (skipDepth > 0) {
            if (!selfClosing) {
                ++skipDepth;
            }
            continue;
        }
        if (stack.empty() || nodeCount >= kMaxNodes) {
            continue;  // outside the root / node budget: ignore
        }
        if (stack.size() >= static_cast<std::size_t>(kMaxDepth)) {
            if (!selfClosing) {
                skipDepth = 1;
            }
            continue;
        }
        current->children.push_back(std::make_unique<Node>());
        Node* childNode = current->children.back().get();
        childNode->name = std::move(name);
        childNode->attrs = std::move(attrs);
        ++nodeCount;
        if (!selfClosing) {
            stack.push_back(childNode);
            current = childNode;
        }
    }
    return root;
}

const std::string* attribute(const Node& node, std::string_view name) {
    for (const Attr& attr : node.attrs) {
        if (equalsIgnoreCase(attr.name, name)) {
            return &attr.value;
        }
    }
    return nullptr;
}

const Node* child(const Node& node, std::string_view name) {
    for (const auto& owned : node.children) {
        if (owned && equalsIgnoreCase(owned->name, name)) {
            return owned.get();
        }
    }
    return nullptr;
}

const Node* descendant(const Node& node, std::string_view name) {
    for (const auto& owned : node.children) {
        if (!owned) {
            continue;
        }
        if (equalsIgnoreCase(owned->name, name)) {
            return owned.get();
        }
        if (const Node* found = descendant(*owned, name)) {
            return found;
        }
    }
    return nullptr;
}

std::string unescape(std::string_view s) { return decode(s); }

}  // namespace koalaamp::playlist::xml
