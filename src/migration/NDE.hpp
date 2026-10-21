// SPDX-License-Identifier: MIT
// migration — Winamp NDE media-library reader (MI-1).
//
// Read-only. The format is the documented one: `NDETABLE` data records and an
// `NDEINDEX` primary index; each record is a linked list of 14-byte field
// headers (column, type, dataSize, next, previous) followed by the payload.
// Unknown/newer field types are skipped and counted, never fatal.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace koalaamp::migration {

enum class NdeType : std::uint8_t {
    Column = 0,
    Index = 1,
    Redirector = 2,
    String = 3,
    Integer = 4,
    Boolean = 5,
    Binary = 6,
    Guid = 7,
    Float = 9,
    DateTime = 10,
    Length = 11,
    Filename = 12,
    Int64 = 13,
    Binary32 = 14,
    Int128 = 15,
};

struct NdeField {
    NdeType type{NdeType::Binary};
    std::string text;       ///< String payload (UTF-8/ANSI bytes as stored)
    std::int64_t number{0}; ///< Integer/Boolean/DateTime/Length/Int64
    double real{0.0};       ///< Float
};

struct NdeRecord {
    /// ml_local column id -> decoded field. Only the first field for a column is
    /// kept; NDE allows duplicate/unknown columns in redirector chains.
    std::map<int, NdeField> fields;
};

struct NdeColumn {
    std::string name; ///< public column name from record 0
    NdeType type{NdeType::Binary};
};

struct NdeDatabase {
    std::vector<NdeRecord> records;
    /// Column definitions (record 0) keyed by column id. `recent.dat` uses a
    /// different schema from `main.dat`, so history import resolves names.
    std::map<int, NdeColumn> columns;
    std::size_t skippedFields{0};  ///< unknown/malformed field payloads
    std::size_t skippedRecords{0}; ///< cyclic/out-of-range record chains
    std::string error;             ///< fatal format/index error; empty on success
    bool ok() const { return error.empty(); }
};

/// Reads `<name>.dat` + `<name>.idx`. A missing/unreadable file is an error;
/// individual bad records are skipped and counted.
NdeDatabase readNde(const std::filesystem::path& dataFile, const std::filesystem::path& indexFile);

/// The ml_local media-track projection used by the migrator.
struct NdeTrack {
    std::string filename;
    std::string title;
    std::string artist;
    std::string album;
    std::string albumArtist;
    std::string genre;
    std::string comment;
    std::string composer;
    int year{0};
    int trackNo{0};
    int discNo{0};
    int rating{0}; ///< 0 = unrated, 1..5
    int playCount{0};
    std::int64_t durationMs{0};
    std::int64_t lastPlayed{0};
    std::int64_t dateAdded{0};
    std::int64_t bitrate{0};
    std::int64_t fileSize{0};
    double albumGain{0.0};
    double trackGain{0.0};
    int bpm{0};
};

/// Maps NDE records to the local-ML track projection. Records without a
/// filename are ignored (column-definition/index records, tombstones).
std::vector<NdeTrack> ndeTracks(const NdeDatabase& database);

/// One row from `recent.dat`: a played track and when. Column names are used
/// when present (with ml_local ids as the fallback), so the same reader handles
/// both databases.
struct NdeHistoryEntry {
    std::string filename;
    std::string title;
    std::string artist;
    std::string album;
    std::int64_t playedAt{0};
};

std::vector<NdeHistoryEntry> ndeHistory(const NdeDatabase& database);

} // namespace koalaamp::migration
