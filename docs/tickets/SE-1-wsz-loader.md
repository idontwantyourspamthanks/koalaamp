# SE-1 · WSZ loader complete

- **Milestone:** M1 · **Est:** 4d · **Depends:** FE-3 · **Status:** in-review (2026-10-11)

## Story
**REUSE, DON'T REIMPLEMENT:** FE-3 already shipped the diff-verified pieces — promote `spikes/fe3_wsz/{ZipReader,BmpDecoder,Skin,SkinSprites}.cpp/hpp` into `src/skins/` as the starting point (zip + RLE8/palette BMP decode, byte-identical to Chrome/Skia on all 18 base-2.91 sheets, and the full 212-sprite table). SE-1's new work is *breadth* (folder containers, text parsers, SkinSet assembly, corpus, fuzz) on top of those proven modules — re-deriving ZIP/RLE8 from scratch re-introduces exactly the bugs FE-3 fixed (see the balance.bmp delta-run trap and the four corrected geometry values in the FE-3 report).

## Tasks
- [x] Container: ZIP + `.wsz` + `.zip` + extracted folders; case-insensitive member lookup by lowercased basename; tolerate one nesting level. — `src/skins/SkinContainer.{hpp,cpp}`; shallowest path wins, equal depth → last member wins (Winamp's extract-over disk order, matching Webamp's `getFileFromZip`).
- [x] BMP decode: 1/4/8/24/32-bit, uncompressed + **BI_RLE8** (common in real skins — standard decoders choke); oversized→crop, undersized→hole (Winamp behavior). — promoted `src/skins/BmpDecoder.{hpp,cpp}`; oversized art keeps its decoded size and the consumer crops to the window, undersized art makes `Skin::spriteRect` report `nullopt` (rendered as a hole). BI_RLE4 is outside the ticket's surface and is rejected (logged) rather than mis-decoded.
- [x] Parse all canonical bitmaps into a `SkinSet` (spec §5.1 list) with per-file fallbacks when optional files are missing. — `assembleSkinSet()`/`loadSkinSet()`; `main.bmp` is the only hard requirement, every other member lands in `missingOptional` with its documented fallback.
- [x] Text parsers (lenient: `//` comments, whitespace, dec/hex): `viscolor.txt` (24 lines), `pledit.txt` (Text section keys), `region.txt` (4 sections, NumPoints/PointList; drop <3-point polygons per Webamp; tolerate space/comma separators). — `src/skins/SkinText.{hpp,cpp}`; behaviour ported from Webamp `utils.ts`/`skinParserUtils.ts`/`regionParser.ts` (MIT) with the SE-1 divergences documented in the header.
- [x] Skin DB: recent skins, current skin, per-skin layout state; drag-drop .wsz onto any window loads it. — `src/skins/SkinDb.{hpp,cpp}` (current + capped MRU + per-skin window layout, line-oriented persistence) and `SkinLoader` (`ISkinLoader::load(path)`), which is the single entry point a dropped `.wsz`/folder will call. **The drop handler itself lands with the SE-2 windows** — no `SkinnedWindow` exists yet (rendering is out of scope for this ticket).
- [x] Corpus test harness: 30-skin golden set + fuzz (truncated zips, zero-size members, bogus BMP headers). — `scripts/fetch-corpus.sh`, `tests/skins_corpus_test.cpp`, `tests/skins_fuzz_test.cpp`.
- [x] Inventory the corpus fixtures with per-skin license notes (research-derived; museum skins stay in test fixtures, never shipped). — see *Corpus inventory* below.

## Acceptance
- [x] All 30 corpus skins load: zero crashes, zero unhandled exceptions; each produces a complete `SkinSet` (missing-optional logged, not fatal). — `corpus: 30 files, 30 loaded, 0 failed` (4435 assertions).
- [x] Parser unit tests: 100% of the documented grammar (per-file fixtures incl. malformed), including the region.txt leniency cases. — `tests/skins_test.cpp`: 34 cases / 156 assertions covering container semantics, BMP 1/4/8/24/32-bit + top-down + RLE8 (delta/absolute+pad/EOB), oversized/undersized geometry, `viscolor`/`pledit`/`region` incl. the mismatched-NumPoints case that crashed real Winamp, SkinSet fallbacks/warnings and SkinDb persistence.
- [x] Fuzz run (100 mutated containers): no crash (may error gracefully). — `fuzz: 100 containers, 28 assembled, 0 threw`; every surviving `SkinSet` re-checked for internal consistency.
- [x] The FE-3 pixel-diff of `base-2.91.wsz` still passes after the loader is "complete" (no regression from leniency). — `spikes/fe3_wsz/render-and-diff.sh`: `0/31900` differing pixels for both the default and toggled state (threshold 0.1 %), on the promoted `src/skins` decode path.

## Evidence

```
$ cmake --preset linux-gcc && cmake --build --preset linux-gcc
$ ctest --preset linux-gcc
100% tests passed, 0 tests failed out of 60        # 23 pre-existing + 37 new

$ ./build/linux-gcc/tests/koalaamp_skins_tests
All tests passed (156 assertions in 34 test cases)
$ ./build/linux-gcc/tests/koalaamp_skins_corpus_tests
All tests passed (4435 assertions in 2 test cases)   # corpus: 30 files, 30 loaded, 0 failed
$ ./build/linux-gcc/tests/koalaamp_skins_fuzz_tests
All tests passed (88 assertions in 1 test case)      # fuzz: 100 containers, 28 assembled, 0 threw

# ASAN + UBSAN parser-path build (-fsanitize=address,undefined -fno-sanitize-recover=all),
# same three binaries, ASAN_OPTIONS=detect_leaks=0 (Qt's own allocations are noisy
# under LSan and are not parser-path bugs):
All tests passed (156 assertions in 34 test cases)
All tests passed (4435 assertions in 2 test cases)
All tests passed (88 assertions in 1 test case)

$ spikes/fe3_wsz/render-and-diff.sh
diff: 0/31900 compared pixels differ = 0.0000% (threshold 0.1000%)   # default
diff: 0/31900 compared pixels differ = 0.0000% (threshold 0.1000%)   # toggled
```

### Promotion record (REUSE, not reimplement)
`ZipReader.{hpp,cpp}`, `BmpDecoder.{hpp,cpp}`, `SkinSprites.hpp`, `Skin.{hpp,cpp}` were
`git mv`-ed from `spikes/fe3_wsz/` into `src/skins/` (history preserved), namespace
`fe3` → `koalaamp::skins`. The spike now links the promoted modules
(`koalaamp_fe3_wsz` keeps only `MainWindowRenderer.cpp`) — no duplicated decoder
remains. Two hardening changes were made to the promoted ZIP/BMP readers for the
fuzz target, both additive and inert on real skins: bounds on the member count and
member size (zip-bomb guard, central-directory sizes used since local headers can
carry 0 for streamed archives) and a 16384-px dimension cap in `bmpInfo`.

## New module surface (`src/skins/`)
| File | Role |
|---|---|
| `SkinContainer.{hpp,cpp}` | `.wsz`/`.zip`/folder container + case-insensitive, last-wins, one-level-nesting lookup |
| `SkinText.{hpp,cpp}` | lenient INI, `viscolor.txt`, `pledit.txt`, `region.txt` |
| `SkinSet.{hpp,cpp}` | canonical file table, per-file fallbacks, diagnostics, `loadSkinSet()` |
| `SkinDb.{hpp,cpp}` | current skin, capped MRU, per-skin window layout + persistence |
| `SkinLoader.{hpp,cpp}` | concrete `ISkinLoader` (`ISkinLoader` also gained `skinSet()`) |
| `SkinInfo.hpp` | Qt-free metadata so the loader interface needs no Qt |
| `Skin.{hpp,cpp}` | promoted: decoded sheet map + sprite lookup/blit (`spriteRect` ↔ undersized-hole) |

## Corpus inventory

`scripts/fetch-corpus.sh` downloads 30 skins (MD5-pinned; each download is
re-hashed on arrival) into the gitignored `third_party/skins-corpus/`, writing
`MANIFEST.tsv` with the museum filename and quirk label. The tests read the
directory and `SKIP` cleanly when it has not been fetched.
Source: Winamp Skin Museum CDN (`https://r2.webampskins.org/skins/<md5>.wsz`);
museum page for skin provenance: `https://skins.webamp.org/skin/<md5>/`.

Quirk coverage: 10 BI_RLE8-heavy skins, 4 with BI_RLE4 members, 1-bit and 4-bit
main sheets, 8/24/32-bit mixes, seven `region.txt`-bearing skins (up to ~7700
vertices / 1925 polygons), four region sections and the no-NumPoints leniency
case, missing `eq_ex`/`eqmain`/`numbers`/`nums_ex`, oversized (307×116, 275×348,
277×118) and undersized (275×112) `main.bmp`, 1×1 `titlebar.bmp`, zero-size
members, folder-nested archives, 24–28 cursor sets, and skins missing every
non-essential bitmap.

**License:** every fixture is third-party user artwork; it is fetched locally,
gitignored, never committed and never shipped (AGENTS.md, `LICENSES.md`). Each
skin remains © its author. Credits below are as stated in the museum's readme
metadata ("—" = no author stated there).

| MD5 (prefix) | Museum file | Credit (museum readme) |
|---|---|---|
| `5e4f1027` | `base-2.91.wsz` | Nullsoft base skin (proprietary artwork) |
| `3296b2c6` | `My_First_Skin_for_RealOne.wsz` | — |
| `9dee1d5b` | `gundam_wing_zero_skin.wsz` | — |
| `c151908a` | `C-AMP2002.wsz` | — |
| `89086b61` | `Tostie_Skin.wsz` | — |
| `c346918d` | `NapsterAmp.wsz` | Ben Shively `<024@home.net>` |
| `546f302a` | `calvin_hobbes.wsz` | Joe Hardy `<hardy@northnet.com.au>` |
| `0050f442` | `Paper-Amp.wsz` | — |
| `a764732f` | `Future_DOS.wsz` | — |
| `412572df` | `2a03.wsz` | "2A03 WA 2.XX SKIN" `<yam_cha@hotmail.com>` |
| `21ac871a` | `WinXP_Amp_Platinum.wsz` | `<shadowfax3000@hotmail.com>` |
| `33ff5ec7` | `Aqua.wsz` | — |
| `3505c140` | `Fun_in_a_UFO.wsz` | "Skin made by spacefem" `<spacefem@spacefem.com>` |
| `3b3b8b07` | `Necromech.wsz` | — |
| `69c6aa22` | `Foot_Fetish.wsz` | — |
| `7b39f961` | `Ghost_Semi-Transparency_Demonstrator.wsz` | — |
| `be2de111` | `Microchip_2_Final_Tranceformation.wsz` | "Author Information" `<vad3k@hotmail.com>` |
| `971c1afc` | `Deus_Ex_Amp_by_AJ.wsz` | `<croc_dumbdee@hotmail.com>` |
| `07d31a29` | `Donkey_Kong_Handheld.wsz` | `<iamdyer@hotmail.com>` |
| `2191f8d6` | `Sheep_in_the_Big_City_1.wsz` | "Made by Nashua" |
| `0eaba96c` | `AMPBOY.wsz` | — |
| `1fc68f13` | `fran_by_malionette.zip` | — |
| `211eb095` | `Star_Trek_console.wsz` | "created by Neil Yriart" |
| `bba81004` | `Simpsons.wsz` | — |
| `0e38ac23` | `ie.wsz` | — |
| `003dee62` | `1.8.wsz` | — |
| `2c4f1e5e` | `Integra_Mugen_Style.wsz` | — |
| `1c3c947d` | `lightning1.wsz` | — |
| `29bc9754` | `Hooplife_Dogslife.wsz` | `<info@hooplife.org>` |
| `4269b10d` | `DOOM_II_AMP.wsz` | `<Annihil8or@seznam.cz>` |

## Deviations / notes
- **BI_RLE4 is not decoded.** The ticket's BMP surface is "1/4/8/24/32-bit,
  uncompressed + BI_RLE8"; a BI_RLE4 sheet (present in a few corpus skins) is
  logged as undecodable and becomes a hole, never mis-decoded. Affects nothing in
  the corpus (all four affected skins keep uncompressed `main.bmp`).
- **`region.txt` is more forgiving than both Winamp and Webamp** where the ticket
  says so: a missing/short `NumPoints` makes the point list one polygon instead of
  dropping the section, and trailing vertices from a mismatched list are kept.
  `<3`-point polygons are still dropped (Webamp rule). Corpus skins whose
  `region.txt` is the all-comments tutorial template parse to an empty region with
  a warning, exactly as Webamp's parser would.
- **`parseIni` merges repeated `[Section]` headers** instead of resetting them
  (Webamp resets); region parsing therefore tolerates repeated section blocks.
- **`viscolor.txt` accepts hex and `//` comments** in addition to Webamp's
  decimal-only parser; skipping an invalid line still compacts the remaining
  colours upward, as Webamp does.
- The **skin-DB drag-drop** half of the Skin DB task is UI and lands with the SE-2
  windows; the DB model + loader entry point are complete and tested here.
- Pixel-diff evidence is Linux/X11 with the same headless-Webamp reference as
  FE-3; Windows/macOS remain unexercised (as FE-3 recorded).

## References
- `spec.md` §5.1 — file list, fallbacks, coordinate-table ownership
- `docs/research/skins.md` §1.1-1.4 — the full format tables incl. RLE8 warning, region.txt sections, gen/genex RGB(0,198,255) marker
- Webamp `skinParser.js` / `regionParser.ts` / `utils.ts` / `skinParserUtils.ts` (MIT, pinned `e6283c0e`) — the leniency behaviour ported here
- `docs/spikes/fe3-wsz-render.md` + `docs/tickets/FE-3-wsz-loader-render.md` — the FE-3 findings this ticket builds on, and the promoted modules now in `src/skins/`

## Notes / risks
- RLE8 is the #1 real-world loader failure (skins.md §1.2 cites eversolo's measurement — Android's own decoder fails on these); dedicated hand-crafted RLE8 tests cover delta, absolute+pad, EOB and short streams before any library was trusted.
- `region.txt` mismatched NumPoints/PointList *crashed real Winamp* — covered by `region survives a NumPoints/PointList mismatch` and the corpus harness, which asserts every retained polygon has ≥3 points.
- A **comment-only** `region.txt` is not a parse failure: the classic base skin ships one (every line a `;` comment — it is documentation for skin authors), and Winamp draws a rectangular window for it. `hasIniContent` gates the "no usable polygons" warning on the file actually carrying a non-comment line, so the canonical skin launches silently (`SkinSet does not warn for a comment-only region.txt`, plus the matching warning case).
