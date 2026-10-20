# LI-3 · Library UI (skinned) + smart views

- **Milestone:** M2 · **Est:** 5d · **Depends:** LI-2, SE-3 · **Status:** in-review — functional; perf/corpus evidence pending

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

Starting a list (library, server browser or playlist) now goes through
`PlaybackCore::playList`, which atomically replaces the queue and session
history and starts the chosen row. This fixes both "jumps to the old global
list" and the end-of-old-track race that could consume the new list's first
queued row (track 2 skipped). The library rebuilds the sequence in the grid's
visible sort order; verified with three four-second WAVs where 01-One advanced
to 02-TWO, and pinned by a `playList` regression test plus the SE-3 interaction
suite.

Still open for acceptance: 100k smart-view perf numbers, five-skin corpus
screenshots, and exact variable-width font/genex widget-art fidelity (the
current list/tree/table styling is palette-based, not per-sprite).

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
