# SE-1 · WSZ loader complete

- **Milestone:** M1 · **Est:** 4d · **Depends:** FE-3 · **Status:** unstarted

## Story
The loader that never says die: every skin in the 100k museum corpus opens without a crash, every documented quirk (RLE8, palettes, case games, missing optional files, folder skins) handled, and the parsed result becomes the single `SkinSet` structure every window renders from. This is the foundation SE-2/SE-3/SE-5 stand on — correctness here is 80% of skin compatibility.

## Tasks
- [ ] Container: ZIP + `.wsz` + `.zip` + extracted folders; case-insensitive member lookup by lowercased basename; tolerate one nesting level.
- [ ] BMP decode: 1/4/8/24/32-bit, uncompressed + **BI_RLE8** (common in real skins — standard decoders choke); oversized→crop, undersized→hole (Winamp behavior).
- [ ] Parse all canonical bitmaps into a `SkinSet` (spec §5.1 list) with per-file fallbacks when optional files are missing.
- [ ] Text parsers (lenient: `//` comments, whitespace, dec/hex): `viscolor.txt` (24 lines), `pledit.txt` (Text section keys), `region.txt` (4 sections, NumPoints/PointList; drop <3-point polygons per Webamp; tolerate space/comma separators).
- [ ] Skin DB: recent skins, current skin, per-skin layout state; drag-drop .wsz onto any window loads it.
- [ ] Corpus test harness: 30-skin golden set + fuzz (truncated zips, zero-size members, bogus BMP headers).
- [ ] Inventory the corpus fixtures with per-skin license notes (research-derived; museum skins stay in test fixtures, never shipped).

## Acceptance
- All 30 corpus skins load: zero crashes, zero unhandled exceptions; each produces a complete `SkinSet` (missing-optional logged, not fatal).
- Parser unit tests: 100% of the documented grammar (per-file fixtures incl. malformed), including the region.txt leniency cases.
- Fuzz run (100 mutated containers): no crash (may error gracefully).
- The FE-3 pixel-diff of `base-2.91.wsz` still passes after the loader is "complete" (no regression from leniency).

## Out of scope
- Rendering any window (SE-2+) — loader only produces `SkinSet`.
- Cursors (`.cur`/`.ani`) — parse paths but UI wiring lands with SE-4 (Winamp skin cursor set is a polish item).
- Modern `.wal` skins — Phase 3.
- Skin editor/authoring features.

## References
- `spec.md` §5.1 — file list, fallbacks, coordinate-table ownership
- `docs/research/skins.md` §1.1-1.4 — the full format tables incl. RLE8 warning, region.txt sections, gen/genex RGB(0,198,255) marker
- Webamp `skinParser.js` / `regionParser.ts` (MIT) — the leniency behavior to port
- `docs/research/formats.md` §1 — playlist-format side of skin folders (not this ticket, but adjacent)

## Notes / risks
- RLE8 is the #1 real-world loader failure (skins.md §1.2 cites eversolo's measurement — Android's own decoder fails on these); write dedicated tests with hand-crafted RLE8 fixtures before trusting any library.
- `region.txt` mismatched NumPoints/PointList *crashed real Winamp* — we must be more forgiving, not equally brittle.
