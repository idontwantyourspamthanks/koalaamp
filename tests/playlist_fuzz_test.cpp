// SPDX-License-Identifier: MIT
//
// PL-1 — playlist parser fuzz pass (the ASAN/UBSAN target, AGENTS.md: parser
// code must be ASAN-clean). 100 deterministically-mutated inputs, each fed
// through every reader and the shared XML scanner, plus a batch of explicit
// pathological shapes (truncation, null bytes, deep nesting, oversized lines,
// a 10k-entry playlist). No input may throw, hang or trip a sanitizer.
#include <catch2/catch_test_macros.hpp>

#include "playlist/Playlist.hpp"
#include "playlist/Xml.hpp"

#include <cstdint>
#include <algorithm>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#ifndef KOALAAMP_PLAYLIST_FIXTURE_DIR
#error "KOALAAMP_PLAYLIST_FIXTURE_DIR must be defined"
#endif

using namespace koalaamp::playlist;

namespace {

std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::string> seeds() {
    const std::filesystem::path dir = KOALAAMP_PLAYLIST_FIXTURE_DIR;
    std::vector<std::string> out;
    for (const char* name : {"emission.m3u8", "leniency.m3u", "sample.pls", "sample.b4s",
                             "sample.xspf", "sample.asx", "sample.wpl", "ml/playlists.xml",
                             "ml/mix-one.m3u"}) {
        out.push_back(slurp(dir / name));
    }
    out.push_back("#EXTM3U\n#EXTINF:1,A\n/a.mp3\n");
    out.push_back("");
    return out;
}

/// Runs every reader over `bytes`; returns nothing. Any exception is a failure.
void feedAll(const std::string& bytes) {
    ReadOptions opts;
    opts.baseDir = "/tmp/koalaamp-fuzz";
    const std::vector<Track> m3u = readM3u(bytes, opts);
    const std::vector<Track> m3u8 = readM3u(bytes, opts);
    const std::vector<Track> pls = readPls(bytes, opts);
    const std::vector<Track> b4s = readB4s(bytes, opts);
    const std::vector<Track> xspf = readXspf(bytes, opts);
    const std::vector<Track> asx = readAsx(bytes, opts);
    const std::vector<Track> wpl = readWpl(bytes, opts);
    const std::vector<IndexEntry> index = readIndex(bytes);
    xml::parse(bytes);
    // The writers must also survive whatever came back.
    (void)writeM3u(m3u, true);
    (void)writeM3u(m3u8, false);
    (void)writePls(pls);
    (void)writeB4s(b4s);
    (void)writeXspf(xspf);
    (void)writeIndex(index);
}

std::string mutate(const std::string& seed, std::mt19937& rng) {
    std::string out = seed;
    const int rounds = 1 + static_cast<int>(rng() % 8);
    for (int r = 0; r < rounds; ++r) {
        if (out.empty()) {
            out.push_back(static_cast<char>(rng() % 256));
            continue;
        }
        switch (rng() % 6) {
            case 0:  // flip a byte
                out[rng() % out.size()] = static_cast<char>(rng() % 256);
                break;
            case 1:  // truncate
                out.resize(rng() % out.size());
                break;
            case 2:  // insert a byte
                out.insert(out.begin() + static_cast<std::ptrdiff_t>(rng() % out.size()),
                           static_cast<char>(rng() % 256));
                break;
            case 3: {  // duplicate a chunk
                const std::size_t at = rng() % out.size();
                const std::size_t len = 1 + rng() % 64;
                out.insert(at, out.substr(at, std::min(len, out.size() - at)));
                break;
            }
            case 4: {  // splice a fixture-like fragment
                static const char* fragments[] = {
                    "#EXTINF:", "#EXTM3U", "<entry playstring=\"", "</playlist>", "[playlist]",
                    "File1=", "<track><location>", "&\x00\xFF", "%20", "\r\n", "<![CDATA[",
                };
                out.insert(rng() % out.size(), fragments[rng() % 11]);
                break;
            }
            case 5:  // delete a range
                if (out.size() > 1) {
                    const std::size_t at = rng() % out.size();
                    const std::size_t len = 1 + rng() % (out.size() - at);
                    out.erase(at, len);
                }
                break;
        }
        if (out.size() > 2u * 1024u * 1024u) {
            out.resize(1024u * 1024u);
        }
    }
    return out;
}

}  // namespace

TEST_CASE("fuzz: 100 mutated playlist inputs never throw", "[pl1][fuzz]") {
    const std::vector<std::string> corpus = seeds();
    REQUIRE_FALSE(corpus.empty());
    std::mt19937 rng(0xC0FFEEu);
    for (int i = 0; i < 100; ++i) {
        const std::string& seed = corpus[rng() % corpus.size()];
        const std::string input = mutate(seed, rng);
        INFO("iteration " << i << " input size " << input.size());
        CHECK_NOTHROW(feedAll(input));
    }
}

TEST_CASE("fuzz: pathological shapes are bounded", "[pl1][fuzz][leniency]") {
    ReadOptions opts;

    // Truncated XML tags / unterminated CDATA / unterminated quote.
    CHECK_NOTHROW(feedAll("<a><b href=\"unterminated"));
    CHECK_NOTHROW(feedAll("<a><![CDATA[never closed"));
    CHECK_NOTHROW(feedAll("</a></b>"));
    CHECK_NOTHROW(feedAll("<<<>>>"));
    CHECK_NOTHROW(feedAll(std::string("\xEF\xBB\xBF", 3)));
    CHECK_NOTHROW(feedAll(std::string("A\0B\0C", 5)));
    CHECK_NOTHROW(feedAll(std::string("\xFF\xFE\x00\x00", 4)));

    // Deeply nested XML (30k levels) must not overflow the stack.
    std::string deep;
    deep.reserve(30000 * 3);
    for (int i = 0; i < 30000; ++i) {
        deep += "<a>";
    }
    CHECK_NOTHROW(feedAll(deep));

    // A single oversized line and an oversized attribute value.
    std::string longLine(1u << 20, 'x');
    longLine = "#EXTINF:1," + longLine + "\n/y.mp3\n";
    const std::vector<Track> longTracks = readM3u(longLine, opts);
    REQUIRE(longTracks.size() == 1);
    CHECK(longTracks[0].title.size() == 1u << 20);

    std::string longAttr = "<entry playstring=\"file:///";
    longAttr += std::string(1u << 20, 'a');
    longAttr += "\"/>";
    CHECK_NOTHROW(readB4s(longAttr, opts));

    // Many malformed directive lines must be skipped, not accumulated.
    std::string junk;
    for (int i = 0; i < 200000; ++i) {
        junk += "#nonsense\n";
    }
    CHECK(readM3u(junk, opts).empty());

    // No terminator, no newline: a media line without a trailing newline.
    const std::vector<Track> noTrailing = readM3u("/music/last.mp3", opts);
    REQUIRE(noTrailing.size() == 1);
    CHECK(noTrailing[0].filename == "/music/last.mp3");
}

TEST_CASE("fuzz: a 10k-entry playlist round-trips under ASAN", "[pl1][fuzz][scale]") {
    std::vector<Track> tracks;
    tracks.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        tracks.push_back({"T" + std::to_string(i), "/m/" + std::to_string(i) + ".mp3", i * 1000});
    }
    const std::string bytes = writeM3u(tracks, true);
    const std::vector<Track> back = readM3u(bytes, ReadOptions{});
    REQUIRE(back.size() == tracks.size());
    CHECK(back == tracks);
}
