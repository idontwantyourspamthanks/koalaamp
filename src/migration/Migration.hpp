// SPDX-License-Identifier: MIT
// migration — end-to-end Winamp import (MI-1). Qt-free; the wizard calls this.

#pragma once

#include "migration/WinampConfig.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace koalaamp::migration {

struct MigrationOptions {
    std::filesystem::path winampDir; ///< empty = auto-detect
    std::filesystem::path databasePath;
};

struct MigrationReport {
    WinampPaths paths;
    WinampState state;
    std::size_t records{0};
    std::size_t matched{0};
    std::size_t unmatched{0};
    std::size_t skippedFields{0};
    std::size_t skippedRecords{0};
    std::vector<std::string> unmatchedPaths;
    std::string error;

    bool ok() const { return error.empty(); }
};

/// Reads `main.dat`/`main.idx`, applies ratings/playcounts/dates/RG/metadata to
/// matching library rows, and reports every path that did not match. Safe to
/// re-run: the database update is value-idempotent.
MigrationReport runMigration(const MigrationOptions& options);

} // namespace koalaamp::migration
