// SPDX-License-Identifier: MIT
//
// SE-1 unit tests: container semantics, BMP depths + RLE8 edge cases, the
// lenient text parsers, SkinSet assembly/fallbacks and the skin DB. All inputs
// are synthetic — no third-party artwork is committed.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "skins/BmpDecoder.hpp"
#include "skins/SkinContainer.hpp"
#include "skins/SkinDb.hpp"
#include "skins/SkinLoader.hpp"
#include "skins/SkinSet.hpp"
#include "skins/SkinText.hpp"
#include "skins_test_helpers.hpp"

using namespace koalaamp::skins;
using skins_test::buildZip;
using skins_test::makeBmp;
using skins_test::solidSheet;
using skins_test::TempDir;
using skins_test::ZipMember;

namespace {

std::vector<std::uint8_t> bytesOf(std::string_view s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

ZipMember member(std::string name, std::vector<std::uint8_t> data) {
    return ZipMember{std::move(name), std::move(data)};
}

/// A compact but structurally complete test skin.
std::vector<ZipMember> testSkinMembers() {
    return {
        member("MAIN.BMP", solidSheet(275, 116, 5)),
        member("nums_ex.bmp", solidSheet(99, 13, 1)),
        member("text.bmp", solidSheet(155, 74, 2)),
        member("viscolor.txt", bytesOf("0,0,0\n24,33,41\n239,49,16\n")),
        member("pledit.txt", bytesOf("[Text]\nNormal=#00FF00\nCurrent=#FFFFFF\nFont=Times\n")),
        member("region.txt", bytesOf("[Normal]\nNumPoints=4\nPointList=0,0 10,0 10,10 0,10\n")),
        member("CLOSE.CUR", bytesOf("\x00\x00\x02\x00")),
        member("readme.txt", bytesOf("hello skin")),
    };
}

SkinContainer containerOf(std::vector<ZipMember> members) {
    std::vector<ContainerEntry> entries;
    entries.reserve(members.size());
    for (auto& m : members) {
        ContainerEntry e;
        e.name = m.name;
        e.data = std::move(m.data);
        entries.push_back(std::move(e));
    }
    return SkinContainer::fromEntries(std::move(entries));
}

}  // namespace

// --- Container ---------------------------------------------------------------

TEST_CASE("container lookup is case-insensitive by basename", "[se1][container]") {
    const std::string base = SkinContainer::normalizeBasename("Folder\\MAIN.BMP");
    CHECK(base == "main.bmp");

    auto container = containerOf({member("SubDir/Main.bmp", bytesOf("nested")),
                                  member("MAIN.BMP", bytesOf("root"))});
    REQUIRE(container.contains("main.bmp"));
    // Shallowest wins: the root member, even though the nested one came first.
    CHECK(std::string(container.findData("main.bmp")->begin(), container.findData("main.bmp")->end()) ==
          "root");
}

TEST_CASE("container allows the last duplicate member to win", "[se1][container]") {
    auto container = containerOf({member("main.bmp", bytesOf("first")),
                                  member("main.bmp", bytesOf("second"))});
    REQUIRE(container.findData("main.bmp") != nullptr);
    CHECK(*container.findData("main.bmp") == bytesOf("second"));
}

TEST_CASE("container finds members nested one level down", "[se1][container]") {
    auto container = containerOf({member("skin/main.bmp", bytesOf("nested")),
                                  member("skin/viscolor.txt", bytesOf("1,2,3"))});
    CHECK(container.contains("main.bmp"));
    CHECK(container.contains("VISCOLOR.TXT"));
    CHECK_FALSE(container.contains("missing.bmp"));
}

TEST_CASE("loadContainer reads zip archives and folders", "[se1][container]") {
    TempDir dir("container");
    const auto zipBytes = buildZip({member("main.bmp", solidSheet(275, 116, 1))});
    const auto zipPath = dir.file("skin.wsz", zipBytes);
    auto fromZip = loadContainer(zipPath);
    REQUIRE(fromZip.has_value());
    CHECK(fromZip->contains("main.bmp"));

    // Extracted-folder form, with the art one level down.
    dir.file("extracted/skin/main.bmp", solidSheet(275, 116, 2));
    auto fromDir = loadContainer(dir.path / "extracted");
    REQUIRE(fromDir.has_value());
    CHECK(fromDir->contains("main.bmp"));

    // A file that is not a ZIP, and a path that does not exist.
    const auto junk = dir.file("not-a-zip.wsz", bytesOf("just text, not a zip at all"));
    CHECK_FALSE(loadContainer(junk).has_value());
    CHECK_FALSE(loadContainer(dir.path / "nope.wsz").has_value());
}

TEST_CASE("loadContainer tolerates a truncated archive", "[se1][container]") {
    TempDir dir("truncated");
    const auto truncated = buildZip({member("main.bmp", solidSheet(275, 116, 1))}, /*truncateBy=*/4);
    const auto p = dir.file("trunc.wsz", truncated);
    // Either parsed (lenient) or rejected — but never a crash.
    (void)loadContainer(p);
    CHECK(true);
}

// --- BMP decode --------------------------------------------------------------

TEST_CASE("BMP 1-bit unpacks MSB-first bits", "[se1][bmp]") {
    // 2x2, stride 4. Stored rows are bottom-up; within a byte the leftmost pixel
    // is the most significant bit.
    // Bottom row byte 0x80 -> pixels {1,0}; top row byte 0x40 -> pixels {0,1}.
    const std::vector<std::uint8_t> pixels = {0x80, 0, 0, 0, 0x40, 0, 0, 0};
    const auto image = decodeBmp(makeBmp(2, 2, 1, 0, pixels));
    REQUIRE(image.has_value());
    CHECK(image->pixelColor(0, 0) == QColor(0, 0, 0));  // top row
    CHECK(image->pixelColor(1, 0) == QColor(1, 1, 1));
    CHECK(image->pixelColor(0, 1) == QColor(1, 1, 1));  // bottom row, stored first
    CHECK(image->pixelColor(1, 1) == QColor(0, 0, 0));
}

TEST_CASE("BMP 4-bit uses high nibble first", "[se1][bmp]") {
    const std::vector<std::uint8_t> pixels = {0x12, 0x00, 0x00, 0x00};  // one row, 2 px
    const auto image = decodeBmp(makeBmp(2, 1, 4, 0, pixels));
    REQUIRE(image.has_value());
    CHECK(image->pixelColor(0, 0) == QColor(1, 1, 1));
    CHECK(image->pixelColor(1, 0) == QColor(2, 2, 2));
}

TEST_CASE("BMP 24- and 32-bit read BGR order", "[se1][bmp]") {
    const std::vector<std::uint8_t> rgb = {10, 20, 30, 0, 0, 0};  // bottom row 1px, padded
    const auto img24 = decodeBmp(makeBmp(1, 1, 24, 0, rgb));
    REQUIRE(img24.has_value());
    CHECK(img24->pixelColor(0, 0) == QColor(30, 20, 10));

    const std::vector<std::uint8_t> rgba = {10, 20, 30, 0xFF};
    const auto img32 = decodeBmp(makeBmp(1, 1, 32, 0, rgba));
    REQUIRE(img32.has_value());
    CHECK(img32->pixelColor(0, 0) == QColor(30, 20, 10));
}

TEST_CASE("BMP top-down (negative height) keeps stored row order", "[se1][bmp]") {
    const std::vector<std::uint8_t> pixels = {3, 4, 0, 0, 1, 2, 0, 0};
    const auto image = decodeBmp(makeBmp(2, 2, 8, 0, pixels, /*topDown=*/true));
    REQUIRE(image.has_value());
    CHECK(image->pixelColor(0, 0) == QColor(3, 3, 3));
    CHECK(image->pixelColor(0, 1) == QColor(1, 1, 1));
}

TEST_CASE("BI_RLE8 absolute runs carry a pad byte", "[se1][bmp]") {
    const std::vector<std::uint8_t> rle = {
        0x00, 0x03, 0x05, 0x06, 0x07, 0x00,  // 3 literals + pad
        0x00, 0x01,
    };
    const auto image = decodeBmp(makeBmp(6, 1, 8, 1, rle));
    REQUIRE(image.has_value());
    CHECK(image->pixelColor(0, 0) == QColor(5, 5, 5));
    CHECK(image->pixelColor(2, 0) == QColor(7, 7, 7));
}

TEST_CASE("BI_RLE8 delta skips pixels, which stay black", "[se1][bmp]") {
    const std::vector<std::uint8_t> rle = {
        0x00, 0x02, 0x02, 0x00,  // delta x=2
        0x01, 0x09,
        0x00, 0x01,
    };
    const auto image = decodeBmp(makeBmp(4, 1, 8, 1, rle));
    REQUIRE(image.has_value());
    CHECK(image->pixelColor(0, 0) == QColor(0, 0, 0));
    CHECK(image->pixelColor(2, 0) == QColor(9, 9, 9));
}

TEST_CASE("decodeBmp rejects malformed and unsupported input", "[se1][bmp]") {
    CHECK_FALSE(decodeBmp({}).has_value());
    CHECK_FALSE(decodeBmp(bytesOf("NOTBMP")).has_value());
    CHECK_FALSE(decodeBmp(bytesOf("BM")).has_value());
    // BI_RLE4 is outside the classic-skin surface; it must fail, not mis-decode.
    CHECK_FALSE(decodeBmp(makeBmp(4, 1, 4, 2, {0x00, 0x01})).has_value());
    // A declared size that no payload can satisfy (truncated BI_RGB).
    auto bmp = makeBmp(8, 8, 8, 0, std::vector<std::uint8_t>(64, 0));
    bmp.resize(60);
    CHECK_FALSE(decodeBmp(bmp).has_value());
}

TEST_CASE("oversized sheets crop and undersized sheets leave holes", "[se1][container]") {
    Skin small;
    const auto smallImage = decodeBmp(solidSheet(275, 112, 4));
    REQUIRE(smallImage.has_value());
    small.addSheet("main.bmp", *smallImage);
    CHECK_FALSE(small.spriteRect("MAIN", "MAIN_WINDOW_BACKGROUND").has_value());  // hole

    Skin large;
    const auto largeImage = decodeBmp(solidSheet(307, 116, 4));
    REQUIRE(largeImage.has_value());
    large.addSheet("main.bmp", *largeImage);
    const auto rect = large.spriteRect("MAIN", "MAIN_WINDOW_BACKGROUND");
    REQUIRE(rect.has_value());
    CHECK(*rect == SpriteRect{0, 0, 275, 116});  // crops to the window
}

// --- Text parsers ------------------------------------------------------------

TEST_CASE("parseIni lower-cases sections and keys, truncating at a second =",
          "[se1][text]") {
    const auto ini = parseIni("[Text]\nNormal=#00FF00\nCurrent=a=b\n// comment\n; other\nFont=\"Arial\"\n");
    REQUIRE(ini.count("text") == 1);
    const auto& text = ini.at("text");
    CHECK(text.at("normal") == "#00FF00");
    CHECK(text.at("current") == "a");  // truncated at the second '='
    CHECK(text.at("font") == "Arial");  // surrounding quotes stripped
}

TEST_CASE("parseIni ignores properties before any section", "[se1][text]") {
    const auto ini = parseIni("Orphan=1\n[Region]\nNumPoints=3\n");
    CHECK(ini.count("orphan") == 0);
    REQUIRE(ini.count("region") == 1);
    CHECK(ini.at("region").at("numpoints") == "3");
}

TEST_CASE("viscolor parses 24 lines with comments, hex and compaction",
          "[se1][text]") {
    std::string text = "// leading comment\n";
    for (int i = 0; i < 25; ++i) {
        text += std::to_string(i) + ",0,0\n";
    }
    text += "255,255,255 // trailing comment\n";
    const VisColor vc = parseVisColor(text);
    CHECK(vc.colors[0] == Rgb{0, 0, 0});
    CHECK(vc.colors[23] == Rgb{23, 0, 0});  // 25th line ignored, array capped at 24

    // A skipped (invalid) line does not consume a slot: the colours compact, as
    // Webamp's parseViscolors does.
    const VisColor compact = parseVisColor("1,2,3\ngarbage\n4,5,6");
    CHECK(compact.colors[0] == Rgb{1, 2, 3});
    CHECK(compact.colors[1] == Rgb{4, 5, 6});

    // Hex components parse as numbers (0x0F0 == 240).
    const VisColor hex = parseVisColor("0xFF,0x10,#0F0");
    CHECK(hex.colors[0] == Rgb{255, 16, 240});
}

TEST_CASE("parseRgbValue accepts hex, bare hex and decimal triples", "[se1][text]") {
    CHECK(parseRgbValue("#00FF00") == Rgb{0, 255, 0});
    CHECK(parseRgbValue("00FF00") == Rgb{0, 255, 0});
    CHECK(parseRgbValue("0x00FF00") == Rgb{0, 255, 0});
    CHECK(parseRgbValue("#FFFFFFFF") == Rgb{255, 255, 255});  // over-long truncates
    CHECK(parseRgbValue("0F0") == Rgb{0, 0, 240});  // short values are numbers (0x0F0)
    CHECK(parseRgbValue("12, 34, 56") == Rgb{12, 34, 56});
    CHECK(parseRgbValue("255 0 9") == Rgb{255, 0, 9});
    CHECK_FALSE(parseRgbValue("").has_value());
    CHECK_FALSE(parseRgbValue("not a colour").has_value());
    CHECK_FALSE(parseRgbValue("1,2").has_value());  // incomplete triple
}

TEST_CASE("viscolor falls back to the built-in colours for a short file",
          "[se1][text]") {
    const VisColor defaults = defaultVisColor();
    const VisColor vc = parseVisColor("1,2,3\n");
    CHECK(vc.colors[0] == Rgb{1, 2, 3});
    CHECK(vc.colors[1] == defaults.colors[1]);
    CHECK(vc.colors[23] == defaults.colors[23]);
}

TEST_CASE("pledit parses [Text] keys and normalises over-long hex",
          "[se1][text]") {
    const PleditColors c = parsePledit(
        "[Text]\nNormal=00FF00\nCurrent=#FFFFFFFF\nNormalBG=0,0,0\nFont=Comic Sans\n");
    CHECK(c.normal == Rgb{0, 255, 0});      // '#' optional
    CHECK(c.current == Rgb{255, 255, 255}); // over-long hex truncated to 6 digits
    CHECK(c.normalBg == Rgb{0, 0, 0});      // decimal triple also accepted
    CHECK(c.selectedBg == defaultPleditColors().selectedBg);  // per-key fallback
    CHECK(c.font == "Comic Sans");
}

TEST_CASE("pledit falls back to defaults on a corrupt file", "[se1][text]") {
    CHECK(parsePledit("not ini at all") == defaultPleditColors());
    CHECK(parsePledit("[Other]\nNormal=#123456\n") == defaultPleditColors());
}

TEST_CASE("region parses NumPoints/PointList with space or comma separators",
          "[se1][text]") {
    const Region r = parseRegion(
        "[Normal]\nNumPoints=4, 4\nPointList=0,0 10,0 10,10 0,10  20,20 30,20 30,30 20,30\n");
    REQUIRE(r.sections.count("normal") == 1);
    const auto& polys = r.sections.at("normal");
    REQUIRE(polys.size() == 2);
    CHECK(polys[0].points.size() == 4);
    CHECK(polys[0].points[1] == RegionPoint{10, 0});
    CHECK(polys[1].points[0] == RegionPoint{20, 20});
    CHECK(r.polygonCount() == 2);
    CHECK(r.pointCount() == 8);
}

TEST_CASE("region drops polygons with fewer than three points", "[se1][text]") {
    // The 2-point "polygon" is skipped and the index advances (Webamp).
    const Region r = parseRegion("[Normal]\nNumPoints=2,3\nPointList=1,1 2,2 5,5 6,6 7,7\n");
    REQUIRE(r.sections.count("normal") == 1);
    const auto& polys = r.sections.at("normal");
    REQUIRE(polys.size() == 1);
    CHECK(polys[0].points.size() == 3);
    CHECK(polys[0].points[0] == RegionPoint{5, 5});
}

TEST_CASE("region survives a NumPoints/PointList mismatch", "[se1][text]") {
    // More polygons declared than points supplied (the case that crashed Winamp):
    // the section stays usable and nothing throws.
    const Region r = parseRegion("[Normal]\nNumPoints=4,4,4\nPointList=0,0 1,0 1,1 0,1\n");
    REQUIRE(r.sections.count("normal") == 1);
    CHECK(r.sections.at("normal").size() == 1);

    // Fewer polygons declared than points supplied: the trailing vertices are
    // kept as a final polygon rather than silently dropped.
    const Region extra =
        parseRegion("[WindowShade]\nNumPoints=3\nPointList=0,0 1,0 1,1 5,5 6,5 6,6\n");
    REQUIRE(extra.sections.count("windowshade") == 1);
    CHECK(extra.sections.at("windowshade").size() == 2);
}

TEST_CASE("region without NumPoints takes the whole list as one polygon",
          "[se1][text]") {
    const Region r = parseRegion("[Equalizer]\nPointList=0,0 4,0 4,4 0,4\n");
    REQUIRE(r.sections.count("equalizer") == 1);
    CHECK(r.sections.at("equalizer").size() == 1);
    CHECK(r.sections.at("equalizer")[0].points.size() == 4);
}

TEST_CASE("region returns empty for junk and keeps sections with <3 points out",
          "[se1][text]") {
    CHECK(parseRegion("").empty());
    CHECK(parseRegion("garbage\n[NoPoints]\nNumPoints=4\n").empty());
    CHECK(parseRegion("[Normal]\nNumPoints=4\nPointList=0,0 1,1\n").empty());
}

TEST_CASE("hasIniContent separates all-comment files from data files", "[se1][text]") {
    // "Was there anything here a parser could have used?" — the difference
    // between a placeholder/doc member and a member we failed to understand.
    CHECK_FALSE(hasIniContent(""));
    CHECK_FALSE(hasIniContent("; [Normal]\n; PointList=0,0 1,0 1,1\n"));
    CHECK_FALSE(hasIniContent("   \n\n;\n"));
    CHECK(hasIniContent("[Normal]\nNumPoints=4\n"));
    CHECK(hasIniContent("NumPoints=4\n"));
}

// --- SkinSet assembly --------------------------------------------------------

TEST_CASE("SkinSet assembles bitmaps and text with fallbacks logged",
          "[se1][structured]") {
    const auto container = buildZip(testSkinMembers());
    TempDir dir("assemble");
    const auto path = dir.file("test.wsz", container);
    auto set = loadSkinSet(path);
    REQUIRE(set.has_value());
    CHECK(set->isComplete());
    CHECK(set->info.mainWidth == 275);
    CHECK(set->info.mainHeight == 116);
    CHECK(set->info.name == "test.wsz");
    CHECK(set->hasSheet("MAIN"));  // case-insensitive lookup of "MAIN.BMP"
    CHECK(set->hasSheet("NUMS_EX"));
    CHECK_FALSE(set->hasSheet("numbers"));  // not in this container
    CHECK(set->hasSheet("text.bmp"));

    // Canonical-but-absent members are logged, not fatal.
    CHECK(std::find(set->missingOptional.begin(), set->missingOptional.end(), "numbers.bmp") !=
          set->missingOptional.end());
    CHECK(std::find(set->missingOptional.begin(), set->missingOptional.end(), "eq_ex.bmp") !=
          set->missingOptional.end());
    CHECK(!set->missingSummary().empty());

    // Text configs parsed.
    CHECK(set->visColor.colors[2] == Rgb{239, 49, 16});
    CHECK(set->pleditColors.font == "Times");
    CHECK(set->region.sections.count("normal") == 1);

    // Cursors and non-canonical text members are captured.
    CHECK(set->hasCursor("CLOSE"));
    CHECK(set->otherText.count("readme.txt") == 1);
    CHECK(set->otherText.at("readme.txt") == "hello skin");
}

TEST_CASE("SkinSet uses built-in colours when the text files are absent",
          "[se1][structured]") {
    const auto set = assembleSkinSet(containerOf({member("main.bmp", solidSheet(275, 116, 3))}),
                                     "inline.wsz");
    REQUIRE(set.has_value());
    CHECK(set->visColor.colors[0] == defaultVisColor().colors[0]);
    CHECK(set->pleditColors == defaultPleditColors());
    CHECK(set->region.empty());
    CHECK_FALSE(set->hasSheet("TITLEBAR"));
    CHECK(set->isComplete());
}

TEST_CASE("SkinSet does not warn for a comment-only region.txt", "[se1][structured]") {
    // The classic base skin ships exactly this: a REGION.TXT whose every line is
    // a comment (documentation for skin authors). Winamp draws a rectangular
    // window for it and reports nothing — so an empty region here is the correct
    // parse, not a failure, and must not be reported as one.
    const auto set = assembleSkinSet(
        containerOf({member("main.bmp", solidSheet(275, 116, 1)),
                     member("region.txt",
                            bytesOf("; I stole this from the Complex skin\n"
                                    "; [Normal]\n"
                                    "; PointList=0,0 1,0 1,1\n"))}),
        "commented-region.wsz");
    REQUIRE(set.has_value());
    CHECK(set->region.empty());
    for (const auto& w : set->warnings) {
        CHECK(w.find("region.txt") == std::string::npos);
    }
}

TEST_CASE("SkinSet warns when region.txt carries data but no usable polygons",
          "[se1][structured]") {
    // Real content that yields no polygon is worth saying out loud.
    const auto set = assembleSkinSet(
        containerOf({member("main.bmp", solidSheet(275, 116, 1)),
                     member("region.txt", bytesOf("[Normal]\nNumPoints=4\n"))}),
        "bad-region.wsz");
    REQUIRE(set.has_value());
    bool warned = false;
    for (const auto& w : set->warnings) {
        warned = warned || (w.find("region.txt") != std::string::npos);
    }
    CHECK(warned);
}

TEST_CASE("SkinSet rejects containers without a usable main.bmp",
          "[se1][structured]") {
    CHECK_FALSE(assembleSkinSet(containerOf({}), "empty.wsz").has_value());
    CHECK_FALSE(assembleSkinSet(containerOf({member("titlebar.bmp", solidSheet(344, 87, 1))}),
                               "no-main.wsz")
                    .has_value());
    // A main.bmp that is not a decodable BMP is a wasted download: reject it.
    CHECK_FALSE(assembleSkinSet(containerOf({member("main.bmp", bytesOf("not a bmp"))}),
                               "bad-main.wsz")
                    .has_value());
}

TEST_CASE("SkinSet warns about undecodable and undersized art", "[se1][structured]") {
    const auto set = assembleSkinSet(
        containerOf({member("main.bmp", solidSheet(275, 116, 1)),
                     member("titlebar.bmp", solidSheet(10, 10, 2)),
                     member("cbuttons.bmp", bytesOf("BM not really a bmp"))}),
        "quirky.wsz");
    REQUIRE(set.has_value());
    bool undersized = false;
    bool undecodable = false;
    for (const auto& w : set->warnings) {
        undersized = undersized || (w.find("titlebar.bmp is undersized") != std::string::npos);
        undecodable = undecodable || (w.find("cbuttons.bmp is not a decodable BMP") != std::string::npos);
    }
    CHECK(undersized);
    CHECK(undecodable);
    CHECK(set->hasSheet("MAIN"));
}

TEST_CASE("SkinSet loads a folder container the same way as a zip",
          "[se1][structured]") {
    TempDir dir("folder-skin");
    dir.file("skin/main.bmp", solidSheet(275, 116, 7));
    dir.file("skin/viscolor.txt", bytesOf("9,9,9\n"));
    auto set = loadSkinSet(dir.path / "skin");
    REQUIRE(set.has_value());
    CHECK(set->isComplete());
    CHECK(set->visColor.colors[0] == Rgb{9, 9, 9});
}

TEST_CASE("SkinLoader reports info and diagnostics through ISkinLoader",
          "[se1][structured]") {
    TempDir dir("loader");
    const auto path = dir.file("skin.wsz", buildZip(testSkinMembers()));
    SkinLoader loader;
    CHECK(loader.skinSet() == nullptr);
    CHECK_FALSE(loader.load(dir.path / "missing.wsz"));
    REQUIRE(loader.load(path));
    const SkinSet* set = loader.skinSet();
    REQUIRE(set != nullptr);
    CHECK(loader.info().mainWidth == 275);
    CHECK(loader.info().name == "skin.wsz");
    CHECK(!loader.missingOptional().empty());
    loader.reset();
    CHECK(loader.skinSet() == nullptr);
}

// --- Skin DB -----------------------------------------------------------------

TEST_CASE("SkinDb recent list dedups and caps", "[se1][db]") {
    TempDir dir("db");
    SkinDb db;
    for (std::size_t i = 0; i < SkinDb::kMaxRecent + 5; ++i) {
        db.noteLoaded(dir.path / ("skin" + std::to_string(i) + ".wsz"));
    }
    CHECK(db.recent().size() == SkinDb::kMaxRecent);
    CHECK(db.current() == dir.path / ("skin" + std::to_string(SkinDb::kMaxRecent + 4) + ".wsz"));

    // Re-loading an existing skin moves it to the front without duplicating.
    const auto first = db.recent().back();
    db.noteLoaded(first);
    CHECK(db.recent().front() == first);
    CHECK(std::count(db.recent().begin(), db.recent().end(), first) == 1);
}

TEST_CASE("SkinDb round-trips current, recent and layout state", "[se1][db]") {
    TempDir dir("db-persist");
    SkinDb db;
    db.noteLoaded(dir.path / "a.wsz");
    db.noteLoaded(dir.path / "b.wsz");
    SkinLayoutState& layout = db.layoutFor(dir.path / "a.wsz");
    layout.main = {10, 20};
    layout.equalizer = {30, 40};
    layout.playlistWidth = 400;
    layout.eqShade = true;

    const auto file = dir.path / "state" / "skindb.txt";
    REQUIRE(db.saveTo(file));

    SkinDb reloaded;
    REQUIRE(reloaded.loadFrom(file));
    CHECK(reloaded.current() == db.current());
    CHECK(reloaded.recent() == db.recent());
    const SkinLayoutState* restored = reloaded.layout(dir.path / "a.wsz");
    REQUIRE(restored != nullptr);
    CHECK(*restored == layout);
}

TEST_CASE("SkinDb load tolerates malformed lines and forgetting works", "[se1][db]") {
    std::istringstream in("koalaamp-skindb 1\n"
                          "current\t/tmp/x.wsz\n"
                          "layout\tbad\tshape\n"
                          "nonsense\n"
                          "recent\t/tmp/y.wsz\n");
    SkinDb db;
    db.load(in);
    CHECK(db.current() == std::filesystem::path("/tmp/x.wsz"));
    REQUIRE(db.recent().size() == 1);
    CHECK(db.layouts().empty());  // the malformed layout line was skipped

    db.noteLoaded("/tmp/y.wsz");
    CHECK(db.forget("/tmp/y.wsz") == 1);
    CHECK(db.recent().empty());
}

TEST_CASE("the visualizer's placement round-trips in its own record", "[skins][db]") {
    koalaamp::skins::SkinDb db;
    auto& state = db.layoutFor("/tmp/skin.wsz");
    state.main = {10, 20};
    state.playlistVisible = false;
    state.visualizer = koalaamp::skins::WindowPlacement{30, 40};
    state.visualizerVisible = false;

    std::ostringstream out;
    db.save(out);
    const std::string text = out.str();
    // The visualizer rides in its own record: a reader that predates it still
    // parses the `layout` line exactly as before (its key is positional).
    CHECK(text.find("layout2\t30\t40\t1\t") != std::string::npos);
    CHECK(text.find("layout\t10\t20") != std::string::npos);

    koalaamp::skins::SkinDb reloaded;
    std::istringstream in(text);
    reloaded.load(in);
    const auto& restored = reloaded.layoutFor("/tmp/skin.wsz");
    CHECK(restored.main.x == 10);
    CHECK(restored.main.y == 20);
    CHECK_FALSE(restored.playlistVisible);
    REQUIRE(restored.visualizer.has_value());
    CHECK(restored.visualizer->x == 30);
    CHECK(restored.visualizer->y == 40);
    CHECK_FALSE(restored.visualizerVisible);
}

TEST_CASE("a database written before the visualizer existed still loads", "[skins][db]") {
    // Eleven fields and no `layout2`: the pre-VZ-1 shape, key last.
    std::istringstream in("current\t/tmp/x.wsz\n"
                          "layout\t1\t2\t3\t4\t5\t6\t275\t0\t0\t/tmp/x.wsz\n");
    koalaamp::skins::SkinDb db;
    db.load(in);
    const auto& state = db.layoutFor("/tmp/x.wsz");
    CHECK(state.main.x == 1);
    CHECK(state.main.y == 2);
    CHECK(state.playlist.x == 5);
    CHECK_FALSE(state.visualizer.has_value());
    CHECK(state.visualizerVisible);
}

TEST_CASE("a stray visualizer record cannot fabricate a layout", "[skins][db]") {
    // `layout2` decorates the record the classic line created. On its own it must
    // not produce one: an all-zero layout would move the classic three to (0,0).
    std::istringstream in("layout2\t30\t40\t0\t/tmp/x.wsz\n");
    koalaamp::skins::SkinDb db;
    db.load(in);
    CHECK(db.layouts().empty());
}
