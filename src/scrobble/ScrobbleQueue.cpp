// SPDX-License-Identifier: MIT
// scrobble — persistent pending scrobbles. See ScrobbleQueue.hpp.

#include "scrobble/ScrobbleQueue.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <utility>

namespace koalaamp::scrobble {
namespace {

using json = nlohmann::json;

json trackToJson(const ScrobbleTrack& track) {
    json out = json::object();
    out["artist"] = track.artist;
    out["title"] = track.title;
    if (!track.album.empty()) {
        out["album"] = track.album;
    }
    if (!track.albumArtist.empty()) {
        out["albumArtist"] = track.albumArtist;
    }
    if (track.durationMs > 0) {
        out["durationMs"] = track.durationMs;
    }
    if (track.trackNumber > 0) {
        out["trackNumber"] = track.trackNumber;
    }
    if (!track.recordingMbid.empty()) {
        out["recordingMbid"] = track.recordingMbid;
    }
    if (!track.releaseMbid.empty()) {
        out["releaseMbid"] = track.releaseMbid;
    }
    if (!track.artistMbids.empty()) {
        out["artistMbids"] = track.artistMbids;
    }
    return out;
}

std::string stringField(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

int intField(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        return 0;
    }
    return it->get<int>();
}

std::int64_t int64Field(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return 0;
    }
    if (it->is_number_integer()) {
        return it->get<std::int64_t>();
    }
    return 0;
}

/// One entry, or nullopt when it cannot be trusted (missing key or identity).
std::optional<PendingScrobble> parseEntry(const json& item) {
    if (!item.is_object()) {
        return std::nullopt;
    }
    const auto trackIt = item.find("track");
    if (trackIt == item.end() || !trackIt->is_object()) {
        return std::nullopt;
    }
    ScrobbleTrack track;
    track.artist = stringField(*trackIt, "artist");
    track.title = stringField(*trackIt, "title");
    if (!track.valid()) {
        return std::nullopt;
    }
    track.album = stringField(*trackIt, "album");
    track.albumArtist = stringField(*trackIt, "albumArtist");
    track.durationMs = intField(*trackIt, "durationMs");
    track.trackNumber = intField(*trackIt, "trackNumber");
    track.recordingMbid = stringField(*trackIt, "recordingMbid");
    track.releaseMbid = stringField(*trackIt, "releaseMbid");
    const auto mbidsIt = trackIt->find("artistMbids");
    if (mbidsIt != trackIt->end() && mbidsIt->is_array()) {
        for (const auto& mbid : *mbidsIt) {
            if (mbid.is_string()) {
                track.artistMbids.push_back(mbid.get<std::string>());
            }
        }
    }

    PendingScrobble entry;
    const std::string service = stringField(item, "service");
    if (service == "listenbrainz") {
        entry.service = ListenService::ListenBrainz;
    } else if (service == "lastfm") {
        entry.service = ListenService::LastFm;
    } else {
        return std::nullopt;
    }
    entry.track = std::move(track);
    entry.timestampUnix = int64Field(item, "timestamp");
    entry.attempts = std::max(0, intField(item, "attempts"));
    entry.dedupKey = stringField(item, "dedupKey");
    if (entry.dedupKey.empty()) {
        entry.dedupKey = listenDedupKey(entry.service, entry.track, entry.timestampUnix);
    }
    return entry;
}

} // namespace

ScrobbleQueue::ScrobbleQueue(std::filesystem::path file) : file_(std::move(file)) {}

bool ScrobbleQueue::load() {
    entries_.clear();
    keys_.clear();

    std::ifstream in(file_, std::ios::binary);
    if (!in) {
        return !std::filesystem::exists(file_);
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    json root;
    try {
        root = json::parse(text);
    } catch (const json::exception&) {
        return false;
    }

    const json* items = nullptr;
    if (root.is_array()) {
        items = &root;
    } else if (root.is_object()) {
        const auto it = root.find("pending");
        if (it != root.end() && it->is_array()) {
            items = &*it;
        }
    }
    if (items == nullptr) {
        return false;
    }

    for (const auto& item : *items) {
        auto entry = parseEntry(item);
        if (!entry.has_value()) {
            continue;
        }
        if (!keys_.insert(entry->dedupKey).second) {
            continue; // The same listen was queued twice; keep the first.
        }
        entries_.push_back(std::move(*entry));
    }
    return true;
}

bool ScrobbleQueue::save() const {
    std::error_code error;
    if (file_.has_parent_path()) {
        std::filesystem::create_directories(file_.parent_path(), error);
        if (error) {
            return false;
        }
    }

    json items = json::array();
    for (const auto& entry : entries_) {
        json item = json::object();
        item["service"] = entry.service == ListenService::LastFm ? "lastfm" : "listenbrainz";
        item["dedupKey"] = entry.dedupKey;
        item["timestamp"] = entry.timestampUnix;
        item["attempts"] = entry.attempts;
        item["track"] = trackToJson(entry.track);
        items.push_back(std::move(item));
    }
    json root = json::object();
    root["version"] = 1;
    root["pending"] = std::move(items);

    std::filesystem::path temporary = file_;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << root.dump(2);
        out.flush();
        if (!out) {
            return false;
        }
    }

    std::filesystem::rename(temporary, file_, error);
    if (error) {
        // Windows does not replace an existing target with rename(); retry after
        // removing it. POSIX takes the atomic path above.
        std::error_code removeError;
        std::filesystem::remove(file_, removeError);
        error.clear();
        std::filesystem::rename(temporary, file_, error);
        if (error) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
    return true;
}

bool ScrobbleQueue::enqueue(PendingScrobble entry) {
    if (entry.dedupKey.empty()) {
        entry.dedupKey = listenDedupKey(entry.service, entry.track, entry.timestampUnix);
    }
    if (!keys_.insert(entry.dedupKey).second) {
        return false;
    }
    entries_.push_back(std::move(entry));
    return true;
}

bool ScrobbleQueue::remove(std::string_view dedupKey) {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) {
        return entry.dedupKey == dedupKey;
    });
    if (it == entries_.end()) {
        return false;
    }
    keys_.erase(std::string(dedupKey));
    entries_.erase(it);
    return true;
}

bool ScrobbleQueue::contains(std::string_view dedupKey) const {
    return keys_.find(std::string(dedupKey)) != keys_.end();
}

bool ScrobbleQueue::recordAttempt(std::string_view dedupKey) {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) {
        return entry.dedupKey == dedupKey;
    });
    if (it == entries_.end()) {
        return false;
    }
    ++it->attempts;
    return true;
}

int ScrobbleQueue::flush(
    const std::function<bool(const std::vector<PendingScrobble>&)>& send) {
    if (entries_.empty()) {
        return 0;
    }
    if (!send(entries_)) {
        for (auto& entry : entries_) {
            ++entry.attempts;
        }
        save();
        return 0;
    }
    const int accepted = static_cast<int>(entries_.size());
    entries_.clear();
    keys_.clear();
    save();
    return accepted;
}

} // namespace koalaamp::scrobble
