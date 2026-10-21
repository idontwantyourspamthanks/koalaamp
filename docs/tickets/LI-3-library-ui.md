# LI-3 · Library UI (skinned) + smart views

- **Milestone:** M2 · **Est:** 5d · **Depends:** LI-2, SE-3 · **Status:** done — all acceptance evidence recorded; genex widgets use the 18-swatch atlas

## Story
The Media Library window a Winamp user expects: artist/album tree, track grid, filters, and the smart views that made the classic ML useful — all rendered in skin chrome (`gen`/`genex` bitmaps with a designed fallback), with the Winamp query grammar driving saved views.

## Tasks
- [x] Library window chrome: `gen.bmp` tiled titlebar/borders (25px segments) + `genex.bmp` widgets + its variable-width font; RGB(0,198,255) marker-color tiling handled; designed fallback layout when a skin omits gen files.
- [x] `genex` color swatches applied to listview/edit/button/scrollbar (the y=0 x=48+ pixel columns — research table).
- [x] Artist → Album → Tracks tree (data from LI-2's GROUP BY queries, lazy-loaded nodes).
- [x] Track grid: columns (title/time/artist/album/rating/playcount/lastplayed), sort, inline rating stars (click), skinned scrollbar.
- [x] Filters bar (genre/year/artist dropdowns).
- [x] Query-grammar parser → SQL: operators `= != < > HAS NOTHAS LIKE BEGINS ENDS ISEMPTY`, `&& || !`, relative dates `[1 week ago]`; syntax errors surface inline (not crashes).
- [x] Default smart views (recently added/played/modified, most played, never played, top rated) as saved queries; user-created views persist.
- [x] Search box with `?`/`query:` prefix behavior (Winamp semantics).
- [x] Rating + playcount/lastplayed update hooks fire on playback events (scheduler → library write, async).

## Acceptance
- Grammar unit tests: every operator, combined expressions, malformed inputs (error, not crash); relative-date fixtures (clock-mocked).
- Default smart views return identical rows to hand-written reference SQL on the 100k synthetic library.
- Tree + grid browse 100k tracks: scroll 60fps, node expand < 100ms p95.
- Rating a track from the grid writes the DB and the star persists across restart; playcount/lastplayed tick after playback.
- Library window renders correctly for corpus skins *without* gen files (fallback) and *with* (5 fixtures).

## Implementation notes
`LibraryWindow` (`src/ui/LibraryWindow.*`) uses the gen frame and samples the
`genex.bmp` swatches at `y=0, x=48+2i` for a light/dark-safe Qt palette. A
`QTreeWidget` lazily adds albums per artist; the `QTableWidget` holds
title/time/artist/album/rating/plays/last-played with numeric sorting via a
`QTableWidgetItem` subclass. Smart views and filters compile through
`library/Query.*` to parameterized SQL; user-created views persist in QSettings
(PC-1 will fold settings into settings.json). Search supports plain FTS,
`?`/`query:` grammar, and inline syntax errors. Rating clicks and play counts
write `LibraryDb::setRating`/`markPlayed`; a second connection in `main` writes
playcount/lastplayed even when the window is closed. Scanning runs on a worker
thread with its own LibraryDb connection (WAL), then refreshes the window.

`lastupd` is the scanner's `mtime` (nanoseconds) scaled to Unix seconds inside
the query compiler, so the smart-view grammar and the hand-written reference SQL
agree; Recently modified orders by `mtime DESC`. Gen windows ignore `region.txt`:
applying the main window's mask cropped the library to the main silhouette on
skins that shape it.

Starting a list (library, server browser or playlist) now goes through
`PlaybackCore::playList`, which atomically replaces the queue and session
history and starts the chosen row. This fixes both "jumps to the old global
list" and the end-of-old-track race that could consume the new list's first
queued row (track 2 skipped). The library rebuilds the sequence in the grid's
visible sort order; verified with three four-second WAVs where 01-One advanced
to 02-TWO, and pinned by a `playList` regression test plus the SE-3 interaction
suite.

## Acceptance evidence (2026-10-21)
- 100k synthetic rows, 20 runs each, limit 5000:
  `genre` p95 35.7 ms, `artist has` 19.4 ms, `album begins` 18.4 ms,
  `title has` 8.3 ms; artist expansion p95 15.4 ms (`albumsForArtist` +
  `artistTracks`) — all under the 100 ms budget.
- `koalaamp_li3_window_tests` renders the null-skin fallback, five corpus skins
  that carry `gen.bmp`, and one corpus skin without it (fallback frame); 120
  assertions across four cases, offscreen, no crash.
- Manual Xvfb pass: scan → 4 rows, artist filter, click-to-rate persisted
  (rating=2), double-click played and advanced `01-One → 02-TWO`; playcounts
  and lastplayed persisted.
- **Genex atlas:** `GenexStyle` maps the 18 swatches (`y=0`, `x=48+2i`) to
  item/background/text/selection/header/scrollbar roles, paints the 47x15
  active/pressed button with a 4 px 9-slice, the eight 14x14 arrows and the
  fixed 14x28 / 28x14 slider strips; `GenexButton` and `GenexScrollBar` are
  wired into the window. A synthetic-atlas test checks all 18 roles and the
  sprite coordinates; the `genex`-carrying corpus skins render with the new
  controls. The old 24-swatch note was corrected in `docs/research/skins.md`.
- **100k scroll frame budget:** `koalaamp_li3_window_tests "[.bench]"` under
  Xvfb/X11 (not offscreen): median **2.4 ms**, p95 **2.6–3.5 ms** across
  three runs (worst observed 5.5 ms) per full window render with 100k rows
  (budget 16.7 ms for 60 fps). Offscreen p95 was **2.9 ms**.
- CI run `39691385336` green on all six jobs (Linux/macOS ctest, Windows
  build); local suite 364/364.
- **Reference SQL:** the 100k fixture is patterned with deterministic
  rating/playcount/dateadded/lastplayed/mtime values, then all six smart-view
  queries (including Recently modified) are compared row-for-row against
  hand-written SQLite SQL (`library_tests`; 130k assertions in the 100k case).
  `lastupd` maps to `(mtime / 1000000000)`: the scanner stores nanoseconds for
  change detection, while the grammar and NDE use Unix seconds.

## Out of scope
- Mass tag editing, album-art grid view, drag-to-playlist (drag reorder is SE-3's; cross-window DnD is a polish ticket later).
- Tag write-back (Phase 2).
- Remote (server) libraries in this window — SV sources appear via the same tree later, Phase 2.5.

## References
- `spec.md` §7.2-7.3, §5.4 — schema, smart views, skinned-window conventions
- `docs/research/formats.md` §5 — the full smart-view grammar + default views; `ml_local` field semantics
- `docs/research/skins.md` §1.2 — gen/genex geometry and the marker color
- koalapaw music windows (`musicArtistsWindow` etc.) — tree-pagination patterns worth borrowing (SV-1 ports them for servers)

## Notes / risks
- The grammar's relative dates must be timezone-stable (UTC storage, local display) — Winamp's behavior here is loose; pick documented-over-faithful if they conflict.
