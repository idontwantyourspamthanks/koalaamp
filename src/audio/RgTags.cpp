// SPDX-License-Identifier: MIT
// audio — minimal clean-room ReplayGain/R128 tag reader. See RgTags.hpp.

#include "audio/RgTags.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace koalaamp::audio {

namespace {

constexpr std::size_t kMaxReadBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaxOggPages = 64;
constexpr std::size_t kMaxOggBytes = 256u * 1024u;
constexpr int kMaxFlacBlocks = 128;

/// Accumulated fields + whether at least one accepted key was seen.
struct Accum {
    RgInfo info;
    bool found{false};
};

// --- little helpers (all bounds-checked by the caller) ---------------------

std::uint32_t readU32LE(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint32_t readU32BE(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint32_t read24BE(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) |
           static_cast<std::uint32_t>(p[2]);
}

/// ID3 "sync safe" 28-bit integer: four bytes each carrying 7 bits, MSB clear.
std::uint32_t readSyncSafe(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0] & 0x7F) << 21) |
           (static_cast<std::uint32_t>(p[1] & 0x7F) << 14) |
           (static_cast<std::uint32_t>(p[2] & 0x7F) << 7) |
           static_cast<std::uint32_t>(p[3] & 0x7F);
}

bool startsWith(const std::uint8_t* p, std::size_t n, const char* magic, std::size_t m) noexcept {
    if (n < m) {
        return false;
    }
    for (std::size_t i = 0; i < m; ++i) {
        if (p[i] != static_cast<std::uint8_t>(magic[i])) {
            return false;
        }
    }
    return true;
}

bool asciiIEquals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::toupper(ca) != std::toupper(cb)) {
            return false;
        }
    }
    return true;
}

/// A decimal integer, nothing else (optional sign, surrounding blanks); used to
/// distinguish the Opus/R128 Q7.8 form from a dB string.
std::optional<int> parsePlainInt(std::string_view s) noexcept {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) {
        --e;
    }
    if (b >= e) {
        return std::nullopt;
    }
    std::size_t i = b;
    const bool neg = (s[i] == '-');
    if (s[i] == '+' || s[i] == '-') {
        ++i;
    }
    if (i >= e) {
        return std::nullopt;
    }
    long long v = 0;
    for (; i < e; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return std::nullopt;
        }
        v = v * 10 + (s[i] - '0');
        if (v > 2147483648LL) {
            return std::nullopt;
        }
    }
    if (neg) {
        v = -v;
    }
    if (v < -2147483648LL || v > 2147483647LL) {
        return std::nullopt;
    }
    return static_cast<int>(v);
}

enum class Key {
    TrackGain,
    AlbumGain,
    TrackPeak,
    AlbumPeak,
    TrackLufs,
    AlbumLufs,
    R128Track,
    R128Album
};

std::optional<Key> keyOf(std::string_view key) noexcept {
    struct Entry {
        std::string_view name;
        Key key;
    };
    static constexpr Entry kTable[] = {
        {"REPLAYGAIN_TRACK_GAIN", Key::TrackGain}, {"REPLAYGAIN_ALBUM_GAIN", Key::AlbumGain},
        {"REPLAYGAIN_TRACK_PEAK", Key::TrackPeak}, {"REPLAYGAIN_ALBUM_PEAK", Key::AlbumPeak},
        {"REPLAYGAIN_TRACK_LUFS", Key::TrackLufs}, {"REPLAYGAIN_ALBUM_LUFS", Key::AlbumLufs},
        {"R128_TRACK_GAIN", Key::R128Track},       {"R128_ALBUM_GAIN", Key::R128Album},
    };
    for (const Entry& e : kTable) {
        if (asciiIEquals(key, e.name)) {
            return e.key;
        }
    }
    return std::nullopt;
}

void applyEntry(Accum& acc, std::string_view key, std::string_view value) {
    const std::optional<Key> kind = keyOf(key);
    if (!kind.has_value()) {
        return;
    }
    switch (*kind) {
    case Key::TrackGain:
        acc.info.trackGainDb = parseGainDb(value);
        break;
    case Key::AlbumGain:
        acc.info.albumGainDb = parseGainDb(value);
        break;
    case Key::TrackPeak:
        acc.info.trackPeak = parseGainDb(value);
        break;
    case Key::AlbumPeak:
        acc.info.albumPeak = parseGainDb(value);
        break;
    case Key::TrackLufs:
        acc.info.trackLufs = parseLufs(value);
        break;
    case Key::AlbumLufs:
        acc.info.albumLufs = parseLufs(value);
        break;
    case Key::R128Track: {
        const std::optional<int> q78 = parsePlainInt(value);
        acc.info.trackGainDb = q78.has_value() ? std::optional<double>(gainFromR128Q78(*q78))
                                               : parseGainDb(value);
        break;
    }
    case Key::R128Album: {
        const std::optional<int> q78 = parsePlainInt(value);
        acc.info.albumGainDb = q78.has_value() ? std::optional<double>(gainFromR128Q78(*q78))
                                               : parseGainDb(value);
        break;
    }
    }
    acc.found = true;
}

// --- Vorbis comments --------------------------------------------------------

/// `data` points at the comment block: vendor_length/vendor/count/[len+entry].
void parseVorbisComment(const std::uint8_t* data, std::size_t size, Accum& acc) {
    if (size < 4) {
        return;
    }
    const std::uint32_t vendorLen = readU32LE(data);
    std::size_t pos = 4;
    if (vendorLen > size - pos) {
        return;
    }
    pos += vendorLen;
    if (size - pos < 4) {
        return;
    }
    const std::uint32_t count = readU32LE(data + pos);
    pos += 4;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (size - pos < 4) {
            return;
        }
        const std::uint32_t len = readU32LE(data + pos);
        pos += 4;
        if (len > size - pos) {
            return;
        }
        const std::string_view entry(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        const std::size_t eq = entry.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        applyEntry(acc, entry.substr(0, eq), entry.substr(eq + 1));
    }
}

// --- FLAC -------------------------------------------------------------------

void parseFlac(const std::uint8_t* data, std::size_t size, Accum& acc) {
    std::size_t pos = 4; // past "fLaC"
    for (int block = 0; block < kMaxFlacBlocks && size - pos >= 4; ++block) {
        const std::uint8_t header = data[pos];
        const bool last = (header & 0x80) != 0;
        const std::uint8_t type = header & 0x7F;
        const std::uint32_t len = read24BE(data + pos + 1);
        pos += 4;
        if (len > size - pos) {
            return; // truncated/absurd declaration
        }
        if (type == 4) { // VORBIS_COMMENT
            parseVorbisComment(data + pos, len, acc);
            return;
        }
        pos += len;
        if (last) {
            return;
        }
    }
}

// --- Ogg (Vorbis / Opus) ----------------------------------------------------

bool packetStartsWith(const std::vector<std::uint8_t>& p, std::size_t off, const char* magic,
                      std::size_t m) noexcept {
    if (p.size() < off + m) {
        return false;
    }
    return startsWith(p.data() + off, p.size() - off, magic, m);
}

void parseOgg(const std::uint8_t* data, std::size_t size, Accum& acc) {
    std::vector<std::uint8_t> current;
    std::vector<std::vector<std::uint8_t>> packets;
    std::size_t pos = 0;
    std::size_t pages = 0;

    while (pos + 27 <= size && pages < kMaxOggPages && pos < kMaxOggBytes && packets.size() < 2) {
        if (!startsWith(data + pos, size - pos, "OggS", 4) || data[pos + 4] != 0) {
            break;
        }
        const std::uint8_t nseg = data[pos + 26];
        const std::size_t lacingPos = pos + 27;
        if (size - lacingPos < nseg) {
            break;
        }
        std::size_t payloadLen = 0;
        for (std::size_t i = 0; i < nseg; ++i) {
            payloadLen += data[lacingPos + i];
        }
        const std::size_t payloadPos = lacingPos + nseg;
        if (payloadLen > size - payloadPos) {
            break; // lacing sum exceeds what the buffer holds
        }
        std::size_t seg = payloadPos;
        for (std::size_t i = 0; i < nseg && packets.size() < 2; ++i) {
            const std::size_t l = data[lacingPos + i];
            current.insert(current.end(), data + seg, data + seg + l);
            seg += l;
            if (l < 255) { // packet terminator
                packets.push_back(std::move(current));
                current.clear();
            }
        }
        pos = payloadPos + payloadLen;
        ++pages;
    }

    if (packets.size() < 2) {
        return;
    }
    const std::vector<std::uint8_t>& id = packets[0];
    const std::vector<std::uint8_t>& comment = packets[1];
    if (packetStartsWith(id, 0, "\x01vorbis", 7)) {
        if (packetStartsWith(comment, 0, "\x03vorbis", 7)) {
            parseVorbisComment(comment.data() + 7, comment.size() - 7, acc);
        }
    } else if (packetStartsWith(id, 0, "OpusHead", 8)) {
        if (packetStartsWith(comment, 0, "OpusTags", 8)) {
            parseVorbisComment(comment.data() + 8, comment.size() - 8, acc);
        }
    }
}

// --- ID3v2 ------------------------------------------------------------------

std::optional<std::string> utf16ToAscii(const std::vector<std::uint16_t>& units, std::size_t begin,
                                        std::size_t end) {
    std::string out;
    out.reserve(end - begin);
    for (std::size_t i = begin; i < end; ++i) {
        if (units[i] > 0x7F) {
            return std::nullopt; // not convertible to ASCII
        }
        out.push_back(static_cast<char>(units[i]));
    }
    return out;
}

struct Txxx {
    std::string desc;
    std::string value;
};

std::optional<Txxx> decodeTxxx(const std::uint8_t* p, std::size_t n) {
    if (n < 1) {
        return std::nullopt;
    }
    const std::uint8_t enc = p[0];
    ++p;
    --n;

    if (enc == 0 || enc == 3) { // latin1 / UTF-8: single-byte NUL terminator
        std::size_t sep = 0;
        while (sep < n && p[sep] != 0x00) {
            ++sep;
        }
        const std::size_t vstart = (sep < n) ? sep + 1 : n;
        std::size_t vend = n;
        while (vend > vstart && p[vend - 1] == 0x00) {
            --vend;
        }
        Txxx out;
        out.desc.assign(reinterpret_cast<const char*>(p), sep);
        out.value.assign(reinterpret_cast<const char*>(p + vstart), vend - vstart);
        return out;
    }

    // UTF-16 (encoding 1 with BOM, or 2 = big-endian). Any other code -> none.
    if (enc != 1 && enc != 2) {
        return std::nullopt;
    }
    bool bigEndian = (enc == 2);
    std::size_t off = 0;
    if (enc == 1 && n >= 2) {
        if (p[0] == 0xFF && p[1] == 0xFE) {
            bigEndian = false;
            off = 2;
        } else if (p[0] == 0xFE && p[1] == 0xFF) {
            bigEndian = true;
            off = 2;
        }
    }
    std::vector<std::uint16_t> units;
    for (std::size_t i = off; i + 1 < n; i += 2) {
        units.push_back(bigEndian ? static_cast<std::uint16_t>((p[i] << 8) | p[i + 1])
                                  : static_cast<std::uint16_t>(p[i] | (p[i + 1] << 8)));
    }
    std::size_t sep = 0;
    while (sep < units.size() && units[sep] != 0x0000) {
        ++sep;
    }
    const std::size_t vstart = (sep < units.size()) ? sep + 1 : units.size();
    std::size_t vend = units.size();
    while (vend > vstart && units[vend - 1] == 0x0000) {
        --vend;
    }
    const std::optional<std::string> desc = utf16ToAscii(units, 0, sep);
    const std::optional<std::string> value = utf16ToAscii(units, vstart, vend);
    if (!desc.has_value() || !value.has_value()) {
        return std::nullopt; // a non-convertible field yields nothing
    }
    Txxx out;
    out.desc = *desc;
    out.value = *value;
    return out;
}

/// Remove one 0x00 after every 0xFF 0x00 (ID3 unsynchronisation).
std::vector<std::uint8_t> deUnsynchronise(const std::uint8_t* p, std::size_t n) {
    std::vector<std::uint8_t> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(p[i]);
        if (p[i] == 0xFF && i + 1 < n && p[i + 1] == 0x00) {
            ++i;
        }
    }
    return out;
}

bool isIdChar(std::uint8_t c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

void handleTxxx(const std::uint8_t* p, std::size_t n, Accum& acc) {
    const std::optional<Txxx> frame = decodeTxxx(p, n);
    if (!frame.has_value()) {
        return;
    }
    applyEntry(acc, frame->desc, frame->value);
}

void parseId3(const std::uint8_t* data, std::size_t size, Accum& acc) {
    if (size < 10) {
        return;
    }
    const std::uint8_t major = data[3];
    const std::uint8_t flags = data[5];
    if (major < 2 || major > 4) {
        return;
    }
    const std::uint32_t regionLen = readSyncSafe(data + 6);
    if (regionLen > size - 10) {
        return; // declared size runs past the buffer
    }
    const std::uint8_t* frames = data + 10;
    std::size_t framesLen = regionLen;
    std::vector<std::uint8_t> deUnsync;
    if ((flags & 0x80) != 0) {
        deUnsync = deUnsynchronise(frames, framesLen);
        frames = deUnsync.data();
        framesLen = deUnsync.size();
    }

    std::size_t pos = 0;
    while (pos < framesLen) {
        const std::size_t headerLen = (major == 2) ? 6 : 10;
        if (framesLen - pos < headerLen || !isIdChar(frames[pos])) {
            break; // padding (or too short)
        }
        const std::size_t idLen = (major == 2) ? 3 : 4;
        bool idOk = true;
        for (std::size_t i = 0; i < idLen; ++i) {
            if (!isIdChar(frames[pos + i])) {
                idOk = false;
                break;
            }
        }
        if (!idOk) {
            break;
        }
        const std::string id(reinterpret_cast<const char*>(frames + pos), idLen);
        std::uint32_t frameLen = 0;
        if (major == 2) {
            frameLen = read24BE(frames + pos + 3);
        } else {
            frameLen = (major == 4) ? readSyncSafe(frames + pos + 4) : readU32BE(frames + pos + 4);
        }
        const std::uint8_t frameFlagsLow = (major == 2) ? 0 : frames[pos + 9];
        pos += headerLen;
        if (frameLen > framesLen - pos) {
            break; // truncated frame
        }
        const std::uint8_t* payload = frames + pos;
        std::size_t payloadLen = frameLen;
        std::vector<std::uint8_t> frameDeUnsync;
        if (major == 4 && (frameFlagsLow & 0x02) != 0) {
            frameDeUnsync = deUnsynchronise(payload, payloadLen);
            payload = frameDeUnsync.data();
            payloadLen = frameDeUnsync.size();
        }
        if (id == "TXXX") {
            handleTxxx(payload, payloadLen, acc);
        }
        pos += frameLen; // advance by the on-disk frame length
    }
}

} // namespace

RgTagRead parseReplayGainTags(const std::uint8_t* data, std::size_t size) {
    RgTagRead out;
    if (data == nullptr || size < 4) {
        return out;
    }

    Accum acc;
    RgTagContainer form = RgTagContainer::None;
    if (startsWith(data, size, "fLaC", 4)) {
        parseFlac(data, size, acc);
        form = RgTagContainer::VorbisComment;
    } else if (startsWith(data, size, "OggS", 4)) {
        parseOgg(data, size, acc);
        form = RgTagContainer::VorbisComment;
    } else if (startsWith(data, size, "ID3", 3)) {
        parseId3(data, size, acc);
        form = RgTagContainer::Id3v2;
    }

    if (acc.found) {
        out.info = acc.info;
        out.container = form;
    }
    return out;
}

RgTagRead readReplayGainTags(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return RgTagRead{};
    }
    std::vector<std::uint8_t> buffer(kMaxReadBytes);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize got = file.gcount();
    if (got <= 0) {
        return RgTagRead{};
    }
    return parseReplayGainTags(buffer.data(), static_cast<std::size_t>(got));
}

RgInfo RgTagSource::forPath(const std::string& path) {
    const auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return it->second;
    }
    const RgTagRead read = readReplayGainTags(path);
    if (m_cache.size() >= kMaxEntries) {
        m_cache.clear(); // bounded: no LRU bookkeeping for a per-track hook
    }
    const auto [inserted, ok] = m_cache.emplace(path, read.info);
    (void)ok;
    return inserted->second;
}

void RgTagSource::clear() {
    m_cache.clear();
}

std::size_t RgTagSource::size() const noexcept {
    return m_cache.size();
}

} // namespace koalaamp::audio
