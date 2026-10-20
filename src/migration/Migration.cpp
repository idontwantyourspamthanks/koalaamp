// SPDX-License-Identifier: MIT
// migration — end-to-end Winamp import. See Migration.hpp.

#include "migration/Migration.hpp"

#include "library/LibraryDb.hpp"
#include "migration/NDE.hpp"

#include <utility>

namespace koalaamp::migration {

MigrationReport runMigration(const MigrationOptions& options) {
    MigrationReport report;
    report.paths = detectWinampPaths(options.winampDir);
    if (!report.paths.found()) {
        report.error = "No Winamp folder found";
        return report;
    }
    report.state = readWinampState(report.paths.iniFile);
    if (options.databasePath.empty()) {
        report.error = "No library database path";
        return report;
    }

    library::LibraryDb database(options.databasePath);
    if (!database.open()) {
        report.error = database.lastError();
        return report;
    }

    const std::filesystem::path data = report.paths.winampDir / "main.dat";
    const std::filesystem::path index = report.paths.winampDir / "main.idx";
    const NdeDatabase nde = readNde(data, index);
    if (!nde.ok()) {
        report.error = nde.error;
        return report;
    }
    report.skippedFields = nde.skippedFields;
    report.skippedRecords = nde.skippedRecords;
    const std::vector<NdeTrack> tracks = ndeTracks(nde);
    report.records = tracks.size();

    for (const NdeTrack& track : tracks) {
        library::MigrationStats stats;
        stats.rating = track.rating;
        stats.playCount = track.playCount;
        stats.lastPlayed = track.lastPlayed > 0 ? track.lastPlayed : -1;
        stats.dateAdded = track.dateAdded > 0 ? track.dateAdded : -1;
        stats.albumGain = track.albumGain;
        stats.trackGain = track.trackGain;
        stats.bpm = track.bpm > 0 ? track.bpm : -1;
        stats.year = track.year > 0 ? track.year : -1;
        stats.trackNo = track.trackNo > 0 ? track.trackNo : -1;
        stats.discNo = track.discNo > 0 ? track.discNo : -1;
        stats.genre = track.genre;
        stats.comment = track.comment;
        stats.albumArtist = track.albumArtist;
        stats.durationMs = track.durationMs > 0 ? track.durationMs : -1;
        stats.bitrate = track.bitrate > 0 ? track.bitrate : -1;
        if (database.applyMigration(track.filename, stats)) {
            ++report.matched;
        } else {
            ++report.unmatched;
            report.unmatchedPaths.push_back(track.filename);
        }
    }
    return report;
}

} // namespace koalaamp::migration
