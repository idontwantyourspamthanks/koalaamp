// SPDX-License-Identifier: MIT
//
// PL-1 — playlist formats unit suite.
//
// Covers, per docs/tickets/PL-1-playlist-formats.md:
//   * byte-exact M3U8 emission against the source-verified fixture (BOM before
//     `#EXTM3U`, `#EXTINF:<secs>,<title>`, UTF-8 titles);
//   * write -> read -> model equality for M3U/M3U8, PLS, B4S, XSPF;
//   * the leniency corpus (BOM, CRLF/LF, unknown directives, `ASF ` prefix,
//     relative-path resolution, missing metadata, trailing whitespace);
//   * ASX/WPL read-only readers;
//   * the Media Library `playlists.xml` index round-trip + import;
//   * autosave save/restore (the kill -9 simulation);
//   * the `src/playlist` layering guard (Qt-free, UI-free).
#include <catch2/catch_test_macros.hpp>

#include "playlist/Playlist.hpp"
#include "playlist/Util.hpp"
#include "playlist/Xml.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef KOALAAMP_PLAYLIST_FIXTURE_DIR
#error "KOALAAMP_PLAYLIST_FIXTURE_DIR must be defined"
#endif
#ifndef KOALAAMP_PLAYLIST_SRC
#error "KOALAAMP_PLAYLIST_SRC must be defined"
#endif

using namespace koalaamp::playlist;

namespace {

std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return data;
}

std::filesystem::path fixture(const std::string& name) {
    return std::filesystem::path(KOALAAMP_PLAYLIST_FIXTURE_DIR) / name;
}

/// A unique, self-cleaning scratch directory for each test case.
class ScratchDir {
public:
    explicit ScratchDir(const std::string& tag) {
        static int counter = 0;
        m_path = std::filesystem::temp_directory_path() /
                 ("koalaamp-pl1-" + tag + "-" + std::to_string(++counter));
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
        std::filesystem::create_directories(m_path, ec);
    }
    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

std::vector<Track> metadataTracks() {
    return {
        {"Café — Trésor", "/music/Chanson/Café — Trésor.mp3", 214000},
        {"Track Two", "/music/Album/Track Two.ogg", 180000},
        {"Unknown Length", "/music/Album/Three.flac", 0},
    };
}

std::vector<std::string> readRecursive(const std::filesystem::path& root) {
    std::vector<std::string> files;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        return files;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (entry.is_regular_file() &&
            (entry.path().extension() == ".cpp" || entry.path().extension() == ".hpp")) {
            files.push_back(slurp(entry.path()));
        }
    }
    return files;
}

}  // namespace

// ---------------------------------------------------------------------------
// M3U / M3U8
// ---------------------------------------------------------------------------

TEST_CASE("M3U8 writer emission is byte-exact", "[pl1][m3u]") {
    const std::vector<Track> tracks = {
        {"Café — Trésor", "/music/Chanson/Café — Trésor.mp3", 214000},
        {"Track Two", "/music/Album/Track Two.ogg", 180000},
        {"Unknown Length", "/music/Album/Three.flac", 0},
        {"", "/music/Album/Bare Filename.wav", 0},
    };
    const std::string expected = slurp(fixture("emission.m3u8"));
    const std::string emitted = writeM3u(tracks, /*utf8=*/true);
    CHECK(emitted == expected);

    // The BOM is exactly `EF BB BF`, immediately before `#EXTM3U`.
    REQUIRE(emitted.size() >= 4);
    CHECK(static_cast<unsigned char>(emitted[0]) == 0xEFu);
    CHECK(static_cast<unsigned char>(emitted[1]) == 0xBBu);
    CHECK(static_cast<unsigned char>(emitted[2]) == 0xBFu);
    CHECK(emitted.compare(3, 7, "#EXTM3U") == 0);

    // The plain M3U is the same emission without the BOM.
    CHECK(writeM3u(tracks, /*utf8=*/false) == expected.substr(3));
}

TEST_CASE("M3U round-trips write -> read", "[pl1][m3u][roundtrip]") {
    std::vector<Track> tracks = metadataTracks();
    // A title containing a comma must survive (the loader splits on the first).
    tracks.push_back({"Hello, World", "/music/comma.mp3", 10000});
    const std::string bytes = writeM3u(tracks, /*utf8=*/false);
    const std::vector<Track> back = readM3u(bytes, ReadOptions{});
    CHECK(back == tracks);
}

TEST_CASE("M3U8 reader strips the BOM and keeps UTF-8 titles", "[pl1][m3u]") {
    const std::string bytes = slurp(fixture("emission.m3u8"));
    const std::vector<Track> tracks = readM3u(bytes, ReadOptions{});
    REQUIRE(tracks.size() == 4);
    CHECK(tracks[0].title == "Café — Trésor");
    CHECK(tracks[0].filename == "/music/Chanson/Café — Trésor.mp3");
    CHECK(tracks[0].lengthMs == 214000);
    CHECK(tracks[1].lengthMs == 180000);
    // No EXTINF -> title derived from the filename.
    CHECK(tracks[3].title == "Bare Filename");
    CHECK(tracks[3].lengthMs == 0);
}

TEST_CASE("M3U leniency corpus", "[pl1][m3u][leniency]") {
    const std::filesystem::path base = fixture("");
    const std::string bytes = slurp(fixture("leniency.m3u"));
    ReadOptions opts;
    opts.baseDir = base;
    const std::vector<Track> tracks = readM3u(bytes, opts);
    REQUIRE(tracks.size() == 5);

    CHECK(tracks[0].title == "First Title");
    CHECK(tracks[0].filename == "/music/first.mp3");
    CHECK(tracks[0].lengthMs == 12000);

    // `#EXTINF:` without a comma -> seconds only, title derived from filename.
    CHECK(tracks[1].title == "no-title-with-len");
    CHECK(tracks[1].lengthMs == 99000);

    // Unknown directives ignored; `ASF ` prefix stripped; URL kept as-is.
    CHECK(tracks[2].filename == "http://example.com/stream");
    CHECK(tracks[2].title == "stream");
    CHECK(tracks[2].lengthMs == 0);

    // Relative path resolved against the playlist's own folder. canonicalKey
    // renders portably, so this holds on Windows too (no backslashes).
    CHECK(tracks[3].filename == canonicalKey(base / "relative/rel track.mp3"));

    // Trailing whitespace trimmed.
    CHECK(tracks[4].filename == "/music/trailing-space.mp3");
}

TEST_CASE("M3U tolerates CRLF, blank lines and a mid-file #UTF8", "[pl1][m3u][leniency]") {
    const std::string bytes =
        "#EXTM3U\r\n"
        "\r\n"
        "#EXTINF:1,CRLF Title\r\n"
        "/music/crlf.mp3\r\n"
        "#UTF8\r\n"
        "/music/no-metadata.ogg\r\n";
    const std::vector<Track> tracks = readM3u(bytes, ReadOptions{});
    REQUIRE(tracks.size() == 2);
    CHECK(tracks[0].title == "CRLF Title");
    CHECK(tracks[0].lengthMs == 1000);
    CHECK(tracks[0].filename == "/music/crlf.mp3");
    CHECK(tracks[1].title == "no-metadata");
    CHECK(tracks[1].filename == "/music/no-metadata.ogg");
}

TEST_CASE("#EXTAUTOSTART is not a Winamp directive", "[pl1][m3u][leniency]") {
    const std::string bytes =
        "#EXTM3U\n"
        "#EXTAUTOSTART\n"
        "/music/only.mp3\n";
    const std::vector<Track> tracks = readM3u(bytes, ReadOptions{});
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].filename == "/music/only.mp3");
}

TEST_CASE("M3U relative paths resolve against the playlist directory", "[pl1][m3u]") {
    const std::filesystem::path base = fixture("relative");
    ReadOptions opts;
    opts.baseDir = base;
    const std::vector<Track> tracks = readM3u(slurp(base / "relative.m3u"), opts);
    REQUIRE(tracks.size() == 2);
    CHECK(tracks[0].filename == canonicalKey(base / "sub/track.mp3"));
    CHECK(tracks[1].filename == canonicalKey(base / "../up.mp3"));
}

TEST_CASE("M3U reads a 10k-entry playlist", "[pl1][m3u][scale]") {
    std::vector<Track> tracks;
    tracks.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        tracks.push_back({"Track " + std::to_string(i),
                          "/music/t" + std::to_string(i) + ".mp3", (i + 1) * 1000});
    }
    const std::string bytes = writeM3u(tracks, /*utf8=*/true);
    const std::vector<Track> back = readM3u(bytes, ReadOptions{});
    REQUIRE(back.size() == tracks.size());
    CHECK(back == tracks);
}

// ---------------------------------------------------------------------------
// PLS
// ---------------------------------------------------------------------------

TEST_CASE("PLS fixture reads", "[pl1][pls]") {
    const std::vector<Track> tracks = readPls(slurp(fixture("sample.pls")), ReadOptions{});
    REQUIRE(tracks.size() == 2);
    CHECK(tracks[0] == Track{"One", "/music/one.mp3", 30000});
    CHECK(tracks[1] == Track{"Two", "/music/two.ogg", 0});
}

TEST_CASE("PLS round-trips write -> read", "[pl1][pls][roundtrip]") {
    std::vector<Track> tracks = metadataTracks();
    const std::string bytes = writePls(tracks);
    const std::vector<Track> back = readPls(bytes, ReadOptions{});
    CHECK(back == tracks);
}

TEST_CASE("PLS leniency: missing title/length, case-insensitive keys", "[pl1][pls][leniency]") {
    const std::string bytes =
        "[PlayList]\n"
        "numberofentries=2\n"
        "FILE1=/music/a.mp3\n"
        "LENGTH1=5\n"
        "file2=/music/b.ogg\n";
    const std::vector<Track> tracks = readPls(bytes, ReadOptions{});
    REQUIRE(tracks.size() == 2);
    CHECK(tracks[0].title == "a");  // derived, no Title key
    CHECK(tracks[0].lengthMs == 5000);
    CHECK(tracks[1].title == "b");
    CHECK(tracks[1].lengthMs == 0);
}

// ---------------------------------------------------------------------------
// B4S
// ---------------------------------------------------------------------------

TEST_CASE("B4S fixture reads", "[pl1][b4s]") {
    const std::vector<Track> tracks = readB4s(slurp(fixture("sample.b4s")), ReadOptions{});
    REQUIRE(tracks.size() == 2);
    CHECK(tracks[0] == Track{"A & B", "/music/a&b.mp3", 45000});
    CHECK(tracks[1] == Track{"Plain", "/music/plain.mp3", 0});
}

TEST_CASE("B4S round-trips write -> read", "[pl1][b4s][roundtrip]") {
    std::vector<Track> tracks = metadataTracks();
    const std::string bytes = writeB4s(tracks);
    const std::vector<Track> back = readB4s(bytes, ReadOptions{});
    CHECK(back == tracks);
}

// ---------------------------------------------------------------------------
// XSPF
// ---------------------------------------------------------------------------

TEST_CASE("XSPF fixture reads", "[pl1][xspf]") {
    const std::vector<Track> tracks = readXspf(slurp(fixture("sample.xspf")), ReadOptions{});
    REQUIRE(tracks.size() == 2);
    CHECK(tracks[0] == Track{"X", "/music/x.mp3", 1000});
    CHECK(tracks[1] == Track{"y", "/music/y.mp3", 2000});  // title derived
}

TEST_CASE("XSPF round-trips write -> read", "[pl1][xspf][roundtrip]") {
    std::vector<Track> tracks = metadataTracks();
    const std::string bytes = writeXspf(tracks);
    const std::vector<Track> back = readXspf(bytes, ReadOptions{});
    CHECK(back == tracks);
}

// ---------------------------------------------------------------------------
// ASX / WPL (read-only)
// ---------------------------------------------------------------------------

TEST_CASE("ASX fixture reads Title/Ref/Duration", "[pl1][asx]") {
    const std::vector<Track> tracks = readAsx(slurp(fixture("sample.asx")), ReadOptions{});
    REQUIRE(tracks.size() == 2);
    CHECK(tracks[0].title == "Stream One");
    CHECK(tracks[0].filename == "http://example.com/a.asf");
    CHECK(tracks[0].lengthMs == 225500);  // 0:03:45.500
    CHECK(tracks[1].filename == "/music/local.wma");
    CHECK(tracks[1].title == "local");
}

TEST_CASE("WPL fixture reads media src refs", "[pl1][wpl]") {
    const std::vector<Track> tracks = readWpl(slurp(fixture("sample.wpl")), ReadOptions{});
    REQUIRE(tracks.size() == 2);
    CHECK(tracks[0].filename == "D:\\Music\\song one.wma");
    CHECK(tracks[0].title == "song one");
    CHECK(tracks[1].filename == "D:\\Music\\song two.mp3");
}

// ---------------------------------------------------------------------------
// Dispatch + file IO
// ---------------------------------------------------------------------------

TEST_CASE("extension list and format detection", "[pl1][dispatch]") {
    CHECK(formatForExtension(".m3u") == Format::M3u);
    CHECK(formatForExtension("M3U8") == Format::M3u8);
    CHECK(formatForExtension(".wax") == Format::Asx);
    CHECK(formatForExtension(".nope") == Format::Unknown);
    CHECK(extensionForFormat(Format::M3u8) == ".m3u8");
    const auto& exts = supportedExtensions();
    CHECK(std::find(exts.begin(), exts.end(), ".m3u") != exts.end());
    CHECK(std::find(exts.begin(), exts.end(), ".pls") != exts.end());
    CHECK(std::find(exts.begin(), exts.end(), ".xspf") != exts.end());
}

TEST_CASE("readFile dispatches on extension and content", "[pl1][dispatch]") {
    ScratchDir scratch("dispatch");
    const std::vector<Track> tracks = metadataTracks();
    struct Case { const char* name; Format format; };
    const Case cases[] = {
        {"p.m3u", Format::M3u},   {"p.m3u8", Format::M3u8}, {"p.pls", Format::Pls},
        {"p.b4s", Format::B4s},   {"p.xspf", Format::Xspf},
    };
    for (const Case& c : cases) {
        REQUIRE(writeFile(scratch.path() / c.name, tracks, c.format));
        const Playlist parsed = readFile(scratch.path() / c.name);
        CHECK(parsed.format == c.format);
        CHECK(parsed.tracks == tracks);
    }

    // Content sniffing when the extension is foreign.
    const std::filesystem::path sniff = scratch.path() / "playlist.dat";
    std::ofstream(sniff, std::ios::binary) << writeM3u(tracks, true);
    const Playlist sniffed = readFile(sniff);
    CHECK(sniffed.format == Format::M3u8);
    CHECK(sniffed.tracks == tracks);
}

TEST_CASE("writeFile refuses read-only formats and readFile bounds input", "[pl1][dispatch]") {
    ScratchDir scratch("io");
    const std::vector<Track> tracks = metadataTracks();
    CHECK_FALSE(writeFile(scratch.path() / "x.asx", tracks, Format::Asx));
    CHECK_FALSE(writeFile(scratch.path() / "x.wpl", tracks, Format::Wpl));
    CHECK_FALSE(writeFile(scratch.path() / "x.dat", tracks, Format::Unknown));

    const std::filesystem::path big = scratch.path() / "big.bin";
    std::ofstream(big, std::ios::binary) << "0123456789";
    CHECK(readFileBytes(big).has_value());
    CHECK_FALSE(readFileBytes(big, 5).has_value());
    // An unrecognised XML root sniffs as an empty Unknown playlist, not a track
    // format — and never throws.
    const std::filesystem::path weird = scratch.path() / "weird.bin";
    std::ofstream(weird, std::ios::binary) << "<foo/>";
    CHECK(readFile(weird).format == Format::Unknown);
    CHECK(readFile(weird).tracks.empty());
}

TEST_CASE("missing files read as empty playlists", "[pl1][dispatch]") {
    const Playlist parsed = readFile(fixture("does-not-exist.m3u"));
    CHECK(parsed.format == Format::Unknown);
    CHECK(parsed.tracks.empty());
}

// ---------------------------------------------------------------------------
// Media Library index
// ---------------------------------------------------------------------------

TEST_CASE("playlists.xml fixture imports index + its M3Us", "[pl1][index]") {
    const std::vector<IndexEntry> entries = readIndex(slurp(fixture("ml/playlists.xml")));
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].filename == "mix-one.m3u");
    CHECK(entries[0].title == "Mix One");
    CHECK(entries[0].songs == 2);
    CHECK(entries[0].seconds == 120);
    CHECK(entries[0].id == "{11111111-2222-4333-8444-555555555555}");

    // Import the referenced playlists (MI-1 consumes this path).
    const std::filesystem::path dir = fixture("ml");
    const std::vector<Track> one = readM3u(slurp(dir / entries[0].filename), ReadOptions{});
    CHECK(one.size() == 2);
    REQUIRE(one.size() == static_cast<std::size_t>(entries[0].songs));
    const std::vector<Track> two = readM3u(slurp(dir / entries[1].filename), ReadOptions{});
    CHECK(two.size() == 1);
}

TEST_CASE("playlists.xml index round-trips write -> read", "[pl1][index]") {
    std::vector<IndexEntry> entries = {
        {"mix-one.m3u", "Mix One", 2, 120, "{11111111-2222-4333-8444-555555555555}"},
        {"a & b.m3u", "A <B>", 0, 0, newGuid()},
    };
    const std::string xml = writeIndex(entries);
    const std::vector<IndexEntry> back = readIndex(xml);
    CHECK(back == entries);
    CHECK(back[1].id == entries[1].id);
    CHECK(back[1].id.front() == '{');
}

TEST_CASE("readIndex tolerates a lowercase root and missing attributes", "[pl1][index][leniency]") {
    const std::string xml =
        "<playlists><playlist filename=\"x.m3u\" songs=\"3\"/></playlists>";
    const std::vector<IndexEntry> entries = readIndex(xml);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].filename == "x.m3u");
    CHECK(entries[0].songs == 3);
    CHECK(entries[0].seconds == 0);
    CHECK(entries[0].title.empty());
}

// ---------------------------------------------------------------------------
// Autosave
// ---------------------------------------------------------------------------

TEST_CASE("autosave restores the playlist and queue after a kill -9", "[pl1][autosave]") {
    ScratchDir scratch("autosave");
    AutosaveState state;
    state.tracks = metadataTracks();
    state.queue = {2, 0};
    state.currentIndex = 1;
    REQUIRE(saveAutosave(scratch.path(), state));

    // The playlist is stored as a BOM'd M3U8 (ticket: "a plain M3U8").
    const std::string raw = slurp(autosavePlaylistPath(scratch.path()));
    REQUIRE(raw.size() >= 3);
    CHECK(static_cast<unsigned char>(raw[0]) == 0xEFu);

    // Simulate kill -9: a stale .tmp from an interrupted write must not win.
    {
        std::ofstream stale(autosavePlaylistPath(scratch.path()).string() + ".tmp", std::ios::binary);
        stale << "garbage";
    }
    const auto restored = loadAutosave(scratch.path());
    REQUIRE(restored.has_value());
    CHECK(*restored == state);
}

TEST_CASE("autosave load is nullopt when nothing is stored", "[pl1][autosave]") {
    ScratchDir scratch("autosave-empty");
    CHECK_FALSE(loadAutosave(scratch.path()).has_value());
}

TEST_CASE("autosave drops out-of-range queue indices", "[pl1][autosave][leniency]") {
    ScratchDir scratch("autosave-oob");
    AutosaveState state;
    state.tracks = metadataTracks();
    saveAutosave(scratch.path(), state);
    // Corrupt the sidecar with indices past the end.
    std::ofstream(autosaveStatePath(scratch.path()), std::ios::trunc)
        << "koalaamp-playlist-state 1\ncurrent 99\nqueue 0 42 1\n";
    const auto restored = loadAutosave(scratch.path());
    REQUIRE(restored.has_value());
    CHECK(restored->currentIndex == -1);
    CHECK(restored->queue == std::vector<std::size_t>{0, 1});
}

// ---------------------------------------------------------------------------
// XML scanner (shared by the XML formats)
// ---------------------------------------------------------------------------

TEST_CASE("XML scanner is tolerant and depth-bounded", "[pl1][xml]") {
    CHECK(xml::parse("").name.empty());
    CHECK(xml::parse("<a><b>text</b></a>").name == "a");
    CHECK(xml::child(xml::parse("<a><b>x</b></a>"), "b") != nullptr);
    CHECK(xml::unescape("a&amp;b&lt;c&gt;d&quot;e&apos;f") == "a&b<c>d\"e'f");
    // Unknown entities pass through; comments/CDATA/PIs are skipped.
    const xml::Node node = xml::parse("<?xml?><a><!-- c --><b><![CDATA[<raw>]]></b></a>");
    REQUIRE(xml::child(node, "b") != nullptr);
    CHECK(xml::child(node, "b")->text == "<raw>");

    // Deep nesting must neither hang nor blow the stack on parse or destroy.
    std::string deep;
    for (int i = 0; i < 5000; ++i) {
        deep += "<a>";
    }
    const xml::Node root = xml::parse(deep);
    CHECK(root.name == "a");
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

TEST_CASE("resolveMediaRef keeps URLs and resolves relative paths", "[pl1][path]") {
    const std::filesystem::path base = fixture("");
    CHECK(resolveMediaRef("http://example.com/x", base) == "http://example.com/x");
    CHECK(resolveMediaRef("//server/share/x.mp3", base) == "//server/share/x.mp3");
    CHECK(resolveMediaRef("C:\\Music\\x.mp3", base) == "C:\\Music\\x.mp3");
    // A rooted entry with a non-empty baseDir must not be rewritten: on
    // Windows that join used to replace all but the drive ("D:/music/x.mp3").
    CHECK(resolveMediaRef("/music/x.mp3", base) == "/music/x.mp3");
#ifdef _WIN32
    // Rooted but driveless is NOT is_absolute() on Windows — the entry is the
    // author's text and passes through untouched (regression: the rooted gate).
    CHECK(resolveMediaRef("\\music\\x.mp3", base) == "\\music\\x.mp3");
#endif
    CHECK(resolveMediaRef("rel/x.mp3", base) == canonicalKey(base / "rel/x.mp3"));
    CHECK(resolveMediaRef("", base).empty());
}

TEST_CASE("titleFromFilename strips the extension", "[pl1][path]") {
    CHECK(titleFromFilename("/music/Artist - Song.mp3") == "Artist - Song");
    CHECK(titleFromFilename("http://example.com/stream") == "stream");
    CHECK(titleFromFilename("noext") == "noext");
    CHECK(titleFromFilename(".hidden") == ".hidden");
}

// ---------------------------------------------------------------------------
// Layering guard (the SE-3 grep pattern, applied to src/playlist)
// ---------------------------------------------------------------------------

TEST_CASE("PL-1 src/playlist is Qt-free and UI-free", "[pl1][guard]") {
    const std::vector<std::string> files = readRecursive(KOALAAMP_PLAYLIST_SRC);
    REQUIRE_FALSE(files.empty());
    for (const std::string& text : files) {
        CHECK(text.find("#include <Q") == std::string::npos);
        CHECK(text.find("#include \"Q") == std::string::npos);
        CHECK(text.find("#include \"ui/") == std::string::npos);
        CHECK(text.find("koalaamp::ui") == std::string::npos);
        CHECK(text.find("#include \"playback/") == std::string::npos);
        CHECK(text.find("<vlc") == std::string::npos);
    }
}
