// SPDX-License-Identifier: MIT
// library — scanner + polling watcher. See Scanner.hpp.

#include "library/Scanner.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <map>
#include <system_error>
#include <taglib/audioproperties.h>
#include <taglib/tfile.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <utility>

namespace koalaamp::library {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string extensionOf(const fs::path& path) {
    std::string ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(ext.begin());
    }
    return lower(std::move(ext));
}

std::optional<double> parseLeadingDouble(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) {
        return std::nullopt;
    }
    return value;
}

int parseLeadingInt(const std::string& text) {
    if (text.empty()) {
        return 0;
    }
    return static_cast<int>(std::strtol(text.c_str(), nullptr, 10));
}

bool stampOf(const fs::path& path, FileStamp& out) {
    std::error_code error;
    const auto writeTime = fs::last_write_time(path, error);
    if (error) {
        return false;
    }
    out.mtime =
        std::chrono::duration_cast<std::chrono::nanoseconds>(writeTime.time_since_epoch()).count();
    out.size = fs::file_size(path, error);
    return !error;
}

/// First non-empty value for `key`, compared case-insensitively.
std::string property(const TagLib::PropertyMap& properties, const char* key) {
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        if (it->first.upper() == TagLib::String(key) && !it->second.isEmpty()) {
            return it->second.front().to8Bit(true);
        }
    }
    return {};
}

/// `folder.*`, `cover.*` or `front.*` in the track's directory, case-insensitive.
std::string detectArt(const fs::path& directory) {
    static constexpr std::array<const char*, 3> bases = {"folder", "cover", "front"};
    static constexpr std::array<const char*, 4> extensions = {".jpg", ".jpeg", ".png", ".webp"};
    std::error_code error;
    std::vector<std::string> names;
    for (fs::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        if (it->is_regular_file(error)) {
            names.push_back(lower(it->path().filename().string()));
        }
    }
    for (const char* base : bases) {
        for (const char* extension : extensions) {
            const std::string wanted = std::string(base) + extension;
            if (std::find(names.begin(), names.end(), wanted) != names.end()) {
                return (directory / wanted).string();
            }
        }
    }
    return {};
}

struct FoundFile {
    fs::path path;
    FileStamp stamp;
};

void collectInto(const fs::path& root, std::vector<FoundFile>& out) {
    std::error_code error;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    for (; !error && it != end; it.increment(error)) {
        const fs::directory_entry& entry = *it;
        if (entry.is_symlink()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (entry.is_directory(error)) {
            if (!error && ((!name.empty() && name.front() == '.') ||
                           fs::exists(entry.path() / ".ignore", error))) {
                it.disable_recursion_pending();
            }
            error.clear();
            continue;
        }
        if (error || !entry.is_regular_file(error)) {
            error.clear();
            continue;
        }
        if (!name.empty() && name.front() == '.') {
            continue;
        }
        if (!isAudioExtension(extensionOf(entry.path()))) {
            continue;
        }
        FileStamp stamp;
        if (!stampOf(entry.path(), stamp)) {
            continue;
        }
        out.push_back({entry.path(), stamp});
    }
}

std::map<std::string, FileStamp> collectStamps(const std::vector<fs::path>& roots) {
    std::vector<FoundFile> files;
    for (const fs::path& root : roots) {
        collectInto(root, files);
    }
    std::map<std::string, FileStamp> stamps;
    for (const FoundFile& file : files) {
        stamps[file.path.string()] = file.stamp;
    }
    return stamps;
}

} // namespace

bool isAudioExtension(const std::string& extension) {
    static constexpr std::array<const char*, 17> kAudio = {
        "mp3", "flac", "ogg", "oga", "opus", "m4a", "aac", "alac", "wav",
        "wv",  "ape",  "mpc", "it",  "mod",  "s3m", "xm",  "umx"};
    return std::find(std::begin(kAudio), std::end(kAudio), extension) != std::end(kAudio);
}

std::optional<ScannedTrack> readTrack(const fs::path& path) {
    std::error_code error;
    if (!fs::is_regular_file(path, error) || error) {
        return std::nullopt;
    }

    ScannedTrack scanned;
    scanned.track.src = SourceId::Local;
    scanned.track.path = path.string();
    scanned.track.id = scanned.track.path;
    scanned.track.title = path.stem().string();
    scanned.format = extensionOf(path);

    FileStamp stamp;
    if (!stampOf(path, stamp)) {
        return std::nullopt;
    }
    scanned.mtime = stamp.mtime;
    scanned.size = stamp.size;
    scanned.artPath = detectArt(path.parent_path());

    TagLib::FileRef ref(path.string().c_str());
    if (!ref.isNull()) {
        if (const TagLib::Tag* tag = ref.tag()) {
            // File::properties() carries the format-specific fields (DISCNUMBER,
            // COMPOSER, ReplayGain). TagLib 1.x's Tag::properties() can omit them,
            // which made the scanner metadata test fail on distro TagLib.
            const TagLib::PropertyMap properties =
                ref.file() != nullptr ? ref.file()->properties() : tag->properties();
            const std::string title = tag->title().to8Bit(true);
            if (!title.empty()) {
                scanned.track.title = title;
            }
            scanned.track.artist = tag->artist().to8Bit(true);
            scanned.track.albumArtist = property(properties, "ALBUMARTIST");
            if (scanned.track.albumArtist.empty()) {
                scanned.track.albumArtist = property(properties, "ALBUM ARTIST");
            }
            if (scanned.track.albumArtist.empty()) {
                scanned.track.albumArtist = scanned.track.artist;
            }
            scanned.track.album = tag->album().to8Bit(true);
            scanned.track.genre = tag->genre().to8Bit(true);
            scanned.track.year = static_cast<int>(tag->year());
            scanned.track.trackNo = static_cast<int>(tag->track());

            scanned.track.discNo = parseLeadingInt(property(properties, "DISCNUMBER"));

            // SC-1: MusicBrainz identities for ListenBrainz. TagLib normalises
            // Vorbis/ID3/MP4 keys to these property names.
            scanned.track.recordingMbid = property(properties, "MUSICBRAINZ_TRACKID");
            if (scanned.track.recordingMbid.empty()) {
                scanned.track.recordingMbid = property(properties, "MUSICBRAINZ_TRACK_ID");
            }
            scanned.track.releaseMbid = property(properties, "MUSICBRAINZ_ALBUMID");
            const std::string artistIds = property(properties, "MUSICBRAINZ_ARTISTID");
            std::string currentId;
            for (std::size_t i = 0; i <= artistIds.size(); ++i) {
                if (i == artistIds.size() || artistIds[i] == ';' || artistIds[i] == '/') {
                    if (!currentId.empty()) {
                        scanned.track.artistMbids.push_back(currentId);
                        currentId.clear();
                    }
                } else if (artistIds[i] != ' ') {
                    currentId.push_back(artistIds[i]);
                }
            }

            scanned.composer = property(properties, "COMPOSER");
            scanned.publisher = property(properties, "PUBLISHER");
            if (scanned.publisher.empty()) {
                scanned.publisher = property(properties, "ORGANIZATION");
            }
            scanned.comment = tag->comment().to8Bit(true);
            if (scanned.comment.empty()) {
                scanned.comment = property(properties, "COMMENT");
            }
            scanned.bpm = parseLeadingInt(property(properties, "BPM"));

            struct RgValues {
                std::optional<double> trackGain;
                std::optional<double> trackPeak;
                std::optional<double> albumGain;
                std::optional<double> albumPeak;
            } rg;
            bool any = false;
            const auto assign = [&](const char* key, std::optional<double>& target) {
                if (const auto value = parseLeadingDouble(property(properties, key))) {
                    target = value;
                    any = true;
                }
            };
            assign("REPLAYGAIN_TRACK_GAIN", rg.trackGain);
            assign("REPLAYGAIN_TRACK_PEAK", rg.trackPeak);
            assign("REPLAYGAIN_ALBUM_GAIN", rg.albumGain);
            assign("REPLAYGAIN_ALBUM_PEAK", rg.albumPeak);
            if (!any) {
                // R128 Q7.8 gain (1/256 dB) is the Opus convention.
                if (const std::string r128 = property(properties, "R128_TRACK_GAIN");
                    !r128.empty()) {
                    const int q78 = parseLeadingInt(r128);
                    rg.trackGain = static_cast<double>(q78) / 256.0;
                    any = true;
                }
            }
            if (any) {
                // `Track::rg` uses plain doubles; unknown members stay 0.
                scanned.track.rg = Rg{rg.trackGain.value_or(0.0), rg.trackPeak.value_or(0.0),
                                      rg.albumGain.value_or(0.0), rg.albumPeak.value_or(0.0)};
            }
        }
        if (const TagLib::AudioProperties* audio = ref.audioProperties()) {
            scanned.track.durationMs = audio->lengthInMilliseconds();
            scanned.track.bitrate = static_cast<std::int64_t>(audio->bitrate()) * 1000;
            scanned.sampleRate = audio->sampleRate();
            scanned.channels = audio->channels();
        }
    }
    return scanned;
}

ScanSummary scanRoots(const std::vector<fs::path>& roots, const ScanCallbacks& callbacks) {
    ScanSummary summary;
    std::vector<FoundFile> files;
    for (const fs::path& root : roots) {
        collectInto(root, files);
    }
    std::sort(files.begin(), files.end(), [](const FoundFile& a, const FoundFile& b) {
        return a.path.string() < b.path.string();
    });

    for (const FoundFile& file : files) {
        if (callbacks.cancelled && callbacks.cancelled()) {
            summary.cancelled = true;
            break;
        }
        ++summary.seen;
        std::optional<FileStamp> known;
        if (callbacks.known) {
            known = callbacks.known(file.path.string());
        }
        if (known.has_value() && *known == file.stamp) {
            ++summary.unchanged;
            continue;
        }
        std::optional<ScannedTrack> scanned = readTrack(file.path);
        if (!scanned.has_value()) {
            ++summary.unreadable;
            continue;
        }
        if (known.has_value()) {
            ++summary.updated;
        } else {
            ++summary.added;
        }
        if (callbacks.onTrack) {
            callbacks.onTrack(*scanned);
        }
    }
    return summary;
}

LibraryWatcher::LibraryWatcher(std::vector<fs::path> roots, std::chrono::milliseconds interval)
    : roots_(std::move(roots)), interval_(interval) {}

LibraryWatcher::~LibraryWatcher() {
    stop();
}

void LibraryWatcher::setOnChange(std::function<void()> callback) {
    onChange_ = std::move(callback);
}

void LibraryWatcher::start() {
    if (running_.exchange(true)) {
        return;
    }
    stop_.store(false);
    thread_ = std::thread([this] { run(); });
}

void LibraryWatcher::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    stop_.store(true);
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void LibraryWatcher::run() {
    std::map<std::string, FileStamp> previous = collectStamps(roots_);
    while (!stop_.load()) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, interval_, [this] { return stop_.load(); });
        }
        if (stop_.load()) {
            break;
        }
        std::map<std::string, FileStamp> current = collectStamps(roots_);
        if (current == previous) {
            continue;
        }
        // Debounce: let a copy-in-progress settle, then report one batch.
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, interval_, [this] { return stop_.load(); });
        }
        if (stop_.load()) {
            break;
        }
        previous = collectStamps(roots_);
        if (onChange_) {
            onChange_();
        }
    }
}

} // namespace koalaamp::library
