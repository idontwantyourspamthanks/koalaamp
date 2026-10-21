// SPDX-License-Identifier: MIT
// migration — Winamp NDE reader. See NDE.hpp.

#include "migration/NDE.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string_view>
#include <utility>

namespace koalaamp::migration {
namespace {

constexpr std::size_t kHeaderBytes = 14;
constexpr std::size_t kMaxRecords = 1'000'000;
constexpr std::size_t kMaxChain = 512;

std::uint16_t u16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t u32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::int64_t s64(const unsigned char* p) {
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | p[i];
    }
    return static_cast<std::int64_t>(value);
}

float f32(const unsigned char* p) {
    const std::uint32_t bits = u32(p);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool readFile(const std::filesystem::path& path, std::vector<unsigned char>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool hasSignature(const std::vector<unsigned char>& bytes, const char* signature) {
    return bytes.size() >= 8 && std::memcmp(bytes.data(), signature, 8) == 0;
}

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
            codepoint = 0xfffd; // lone low surrogate
        }
        appendUtf8(out, codepoint);
    }
    return out;
}

/// NDE strings are a u16 byte count followed by either UTF-8/ANSI bytes or a
/// UTF-16LE/BE BOM-prefixed string. `available` is the payload after the u16.
std::string decodeString(const unsigned char* data, std::size_t available, std::size_t length) {
    const std::size_t bytes = std::min(available, length);
    if (bytes >= 2 && data[0] == 0xff && data[1] == 0xfe) {
        return utf16ToUtf8(data + 2, bytes - 2, true);
    }
    if (bytes >= 2 && data[0] == 0xfe && data[1] == 0xff) {
        return utf16ToUtf8(data + 2, bytes - 2, false);
    }
    return std::string(reinterpret_cast<const char*>(data), bytes);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

int columnId(const NdeDatabase& database, std::initializer_list<std::string_view> names,
             int fallback) {
    for (const auto& [id, column] : database.columns) {
        const std::string name = lower(column.name);
        for (std::string_view wanted : names) {
            if (name == wanted) {
                return id;
            }
        }
    }
    return fallback;
}

} // namespace

NdeDatabase readNde(const std::filesystem::path& dataFile, const std::filesystem::path& indexFile) {
    NdeDatabase database;
    std::vector<unsigned char> data;
    std::vector<unsigned char> index;
    if (!readFile(dataFile, data)) {
        database.error = "cannot read " + dataFile.string();
        return database;
    }
    if (!readFile(indexFile, index)) {
        database.error = "cannot read " + indexFile.string();
        return database;
    }
    if (!hasSignature(data, "NDETABLE")) {
        database.error = "bad NDE data signature";
        return database;
    }
    if (!hasSignature(index, "NDEINDEX")) {
        database.error = "bad NDE index signature";
        return database;
    }
    if (index.size() < 12) {
        database.error = "truncated NDE index header";
        return database;
    }
    const std::uint32_t count = u32(index.data() + 8);
    if (count > kMaxRecords || 12 + static_cast<std::size_t>(count) * 4 > index.size()) {
        database.error = "implausible NDE record count";
        return database;
    }

    database.records.reserve(count);
    for (std::uint32_t recordIndex = 0; recordIndex < count; ++recordIndex) {
        const std::size_t offset = u32(index.data() + 12 + recordIndex * 4);
        if (offset < 8 || offset + kHeaderBytes > data.size()) {
            ++database.skippedRecords;
            continue;
        }
        NdeRecord record;
        std::set<std::size_t> visited;
        std::size_t cursor = offset;
        bool bad = false;
        for (std::size_t step = 0; step < kMaxChain; ++step) {
            if (cursor < 8 || cursor + kHeaderBytes > data.size() ||
                !visited.insert(cursor).second) {
                bad = true;
                break;
            }
            const unsigned char* header = data.data() + cursor;
            const int column = header[0];
            const auto type = static_cast<NdeType>(header[1]);
            const std::uint32_t size = u32(header + 2);
            const std::uint32_t next = u32(header + 6);
            const std::size_t payload = cursor + kHeaderBytes;
            if (payload + size > data.size()) {
                bad = true;
                break;
            }

            NdeField field;
            field.type = type;
            bool decoded = true;
            bool store = true;
            switch (type) {
            case NdeType::Column: {
                store = false;
                if (size < 3) {
                    decoded = false;
                    break;
                }
                const auto declared = static_cast<NdeType>(data[payload]);
                const std::size_t nameLength = data[payload + 2];
                const std::size_t available = size - 3;
                database.columns[column] = {
                    std::string(reinterpret_cast<const char*>(data.data() + payload + 3),
                                std::min(nameLength, available)),
                    declared};
                break;
            }
            case NdeType::String:
            case NdeType::Filename: {
                if (size < 2) {
                    decoded = false;
                    break;
                }
                const std::uint16_t length = u16(data.data() + payload);
                field.text = decodeString(data.data() + payload + 2, size - 2, length);
                break;
            }
            case NdeType::Integer:
            case NdeType::DateTime:
            case NdeType::Length:
                if (size < 4) {
                    decoded = false;
                    break;
                }
                field.number = u32(data.data() + payload);
                break;
            case NdeType::Boolean:
                if (size < 1) {
                    decoded = false;
                    break;
                }
                field.number = data[payload] != 0 ? 1 : 0;
                break;
            case NdeType::Float:
                if (size < 4) {
                    decoded = false;
                    break;
                }
                field.real = f32(data.data() + payload);
                break;
            case NdeType::Int64:
                if (size < 8) {
                    decoded = false;
                    break;
                }
                field.number = s64(data.data() + payload);
                break;
            case NdeType::Index:
            case NdeType::Redirector:
            case NdeType::Binary:
            case NdeType::Guid:
            case NdeType::Binary32:
            case NdeType::Int128:
                decoded = false;
                break;
            default:
                decoded = false;
                break;
            }
            if (decoded && store) {
                record.fields.emplace(column, std::move(field));
            } else if (!decoded) {
                ++database.skippedFields;
            }
            if (next == 0) {
                break;
            }
            cursor = next;
        }
        if (bad) {
            ++database.skippedRecords;
        } else {
            database.records.push_back(std::move(record));
        }
    }
    return database;
}

std::vector<NdeTrack> ndeTracks(const NdeDatabase& database) {
    std::vector<NdeTrack> tracks;
    tracks.reserve(database.records.size());
    for (const NdeRecord& record : database.records) {
        const auto filename = record.fields.find(0);
        if (filename == record.fields.end() || filename->second.text.empty()) {
            continue;
        }
        NdeTrack track;
        track.filename = filename->second.text;
        const auto text = [&record](int column) -> std::string {
            const auto it = record.fields.find(column);
            return it == record.fields.end() ? std::string{} : it->second.text;
        };
        const auto number = [&record](int column) -> std::int64_t {
            const auto it = record.fields.find(column);
            return it == record.fields.end() ? 0 : it->second.number;
        };
        const auto real = [&record](int column) -> double {
            const auto it = record.fields.find(column);
            if (it == record.fields.end()) {
                return 0.0;
            }
            return it->second.type == NdeType::Float ? it->second.real
                                                     : static_cast<double>(it->second.number);
        };
        track.title = text(1);
        track.artist = text(2);
        track.album = text(3);
        track.year = static_cast<int>(number(4));
        track.genre = text(5);
        track.comment = text(6);
        track.trackNo = static_cast<int>(number(7));
        track.durationMs = number(8) * 1000; // Winamp stores seconds
        track.lastPlayed = number(11);
        track.rating = std::clamp(static_cast<int>(number(12)), 0, 5);
        track.playCount = static_cast<int>(number(15));
        track.fileSize = number(17);
        track.bitrate = number(18);
        track.discNo = static_cast<int>(number(19));
        track.albumArtist = text(20);
        track.albumGain = real(21);
        track.trackGain = real(22);
        track.composer = text(24);
        track.bpm = static_cast<int>(number(25));
        track.dateAdded = number(41);
        tracks.push_back(std::move(track));
    }
    return tracks;
}

std::vector<NdeHistoryEntry> ndeHistory(const NdeDatabase& database) {
    const int filenameId = columnId(database, {"filename", "file", "path", "location", "url"}, 0);
    const int titleId = columnId(database, {"title", "name"}, 1);
    const int artistId = columnId(database, {"artist"}, 2);
    const int albumId = columnId(database, {"album"}, 3);
    const int playedId = columnId(database,
                                  {"lastplay", "lastplayed", "last_played", "played", "playtime",
                                   "playdate", "date", "timestamp"},
                                  11);

    const auto text = [](const NdeRecord& record, int column) -> std::string {
        const auto it = record.fields.find(column);
        if (it == record.fields.end()) {
            return {};
        }
        if (it->second.type == NdeType::String || it->second.type == NdeType::Filename) {
            return it->second.text;
        }
        return {};
    };
    const auto number = [](const NdeRecord& record, int column) -> std::int64_t {
        const auto it = record.fields.find(column);
        return it == record.fields.end() ? 0 : it->second.number;
    };

    std::vector<NdeHistoryEntry> history;
    history.reserve(database.records.size());
    for (const NdeRecord& record : database.records) {
        NdeHistoryEntry entry;
        entry.filename = text(record, filenameId);
        if (entry.filename.empty()) {
            continue;
        }
        entry.title = text(record, titleId);
        entry.artist = text(record, artistId);
        entry.album = text(record, albumId);
        entry.playedAt = number(record, playedId);
        if (entry.playedAt <= 0) {
            continue; // a history row without a timestamp cannot be ordered
        }
        if (entry.title.empty()) {
            entry.title = std::filesystem::path(entry.filename).stem().string();
        }
        history.push_back(std::move(entry));
    }
    return history;
}

} // namespace koalaamp::migration
