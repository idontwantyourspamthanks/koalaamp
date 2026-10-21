# MI-1 · Winamp migration wizard

- **Milestone:** M2 · **Est:** 4d · **Depends:** LI-3, AU-1, PL-1 · **Status:** in-progress — code complete for the import surface; real-donation fixture + ASAN gate pending

## Story
The on-ramp for the exact user this product is for: someone with 15 years of Winamp state. One wizard reads their real `%APPDATA%\Winamp` — playlists, the NDE media-library database (ratings, playcounts, last-played, ReplayGain), EQ presets, bookmarks, preferences — and merges it into our library idempotently, with a report of what didn't match.

## Tasks
- [x] Detection: `%APPDATA%\Winamp` (honor `paths.ini` redirection: `inidir`, `inifile`, `m3udir`); "no Winamp found" → friendly empty state.
- [x] NDE reader (read-only): `main.dat`/`main.idx` — NDETABLE/NDEINDEX signatures, field IDs 0-41, record/column linked-list walk, u16-length strings, type tolerance (unknown types → skip field, count it).
- [x] Import mapping: filename (normalized path join), rating, playcount, lastplay, dateadded, RG gains/peaks, bpm, genre/comment → upsert onto scanned tracks by normalized path (case-insensitive on Windows/mac, sensitive on Linux + case-fallback pass).
- [x] Recent/history NDE (`recent.dat`/`recent.idx`) → `play_history`; LibraryWindow History node and migration report.
- [x] Playlists: current `winamp.m3u`/`.m3u8`; `plugins\ml\playlists\playlists.xml` index and its `.m3u` files (PL-1 readers, appended to the live playlist).
- [x] `winamp.ini`: volume, EQ state/preamp/limiter/eq_type, skin path, `config_titlefmt`, shuffle/repeat, windowshade flags.
- [x] `winamp.q1` EQ presets (wizard routes to SE-5).
- [x] `winamp.bm` bookmarks → live playlist entries; UTF-8/UTF-16, `title|filename`, bare paths.
- [x] Skins: `%APPDATA%\Winamp\Skins` offered; classic `.wsz` files copy into the app skin folder and the Skins menu scans it.
- [x] Wizard UI (skinned, 3 steps): source summary → options/import → unmatched report with export.
- [x] Idempotency: re-run upserts (no duplicate rows); the wizard re-runs safely. Last-source state in settings is still pending.

## Acceptance
- Fixture NDE database (generated from node-nde test data + one real-donation sample) imports 100% of documented fields.
- Idempotent: second run changes nothing (row counts + fields identical).
- Unmatched rate < 5% on the clean sample; every unmatched path appears in the report.
- `winamp.q1` presets appear in SE-5's preset panel; `winamp.bm` bookmarks load.
- ASAN clean over the NDE reader running the fuzz corpus (truncated/malformed `.dat`/`.idx`).

## Implementation notes (2026-10-20)
`migration/NDE` is a read-only `NDETABLE`/`NDEINDEX` parser: signature checks,
index-bounded record walk, field-chain cycle guard, all documented scalar types,
u16-length strings, and unknown/newer types skipped and counted. `migration/WinampConfig`
reads `paths.ini` redirection and the `winamp.ini` player-state keys; detection
probes `%APPDATA%/Winamp` and the Wine profile and accepts an explicit folder for
the wizard/tests. `LibraryDb::applyMigration` writes rating/playcount/lastplay/
dateadded/RG/bpm/genre/comment/album-artist/year/track/disc/duration/bitrate by
path, with a case-insensitive fallback pass, and is idempotent. Suites
`migration_tests` (generated NDE + corruption smoke + config) and
`library_tests` (apply-twice). `migration/Migration` now runs the full
NDE→library path and returns matched/unmatched counts, unmatched paths,
skipped-field/record counts, and the parsed `winamp.ini` state; the end-to-end
suite re-runs it to prove idempotency.

The 3-step wizard (`ui/MigrationDialog`) and the unmatched export are in. It
routes the current `winamp.m3u`/`.m3u8` through the existing PL-1 reader and
`winamp.q1` through SE-5's EQF importer; a missing/corrupt NDE database is
reported without blocking those two. `migration_dialog_tests` covers folder
validation, option routing and the report offscreen.

The ML `playlists.xml` index is read with PL-1's `readIndex` and each referenced
M3U is appended through `readFile`; the current playlist and EQ presets are
routed separately, so a failure in one does not cancel the others. The Skins
option copies `.wsz` files into `AppDataLocation/skins` and the menu scans that
folder. `winamp.bm` is parsed as a BOM-aware text list (`title|filename`, bare paths,
escaped pipes) and appended to the live playlist; the wizard reports what
loaded. `recent.dat`/`recent.idx` use the same NDE reader, resolve columns by
name (with ml_local ids as fallback), and key `play_history` on
`(path, played_at, source)` so a second import adds no rows; LibraryWindow adds
a History node over `LibraryDb::history()`.

The documented field acceptance still needs a real donation fixture; the
generated fixture covers all documented scalar types, history columns, UTF-16
strings and the corruption smoke. ASAN fuzz remains the final gate.

## Out of scope
- Writing back to Winamp's files (one-way, always — we never touch a real Winamp install's data destructively).
- `studio.xnf` (modern-skin state) — Phase 3 alongside modern skins.
- iTunes XML import (`ml_impex` equivalent) — v2.

## References
- `spec.md` §9 — the full migration surface
- `docs/research/formats.md` §2-3 — NDE format (signatures, field types, ml_local column list 0-41), winamp.ini keys, q1/bm
- node-nde (MIT) — reader semantics reference for test-fixture generation
- `docs/tickets/PL-1-playlist-formats.md`-equivalent index entry + SE-5 preset panel (consumers)

## Notes / risks
- NDE field 13/14/15 (INT64/BINARY32/INT128) exist in later Winamps — parser must tolerate, skip, and count unknowns rather than fail (formats.md §2).
- Path joining is the #1 source of unmatched rows (drive letters, moved folders); do a second case-insensitive/relative pass before reporting unmatched.
