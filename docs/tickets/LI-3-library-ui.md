# LI-3 · Library UI (skinned) + smart views

- **Milestone:** M2 · **Est:** 5d · **Depends:** LI-2, SE-3 · **Status:** unstarted

## Story
The Media Library window a Winamp user expects: artist/album tree, track grid, filters, and the smart views that made the classic ML useful — all rendered in skin chrome (`gen`/`genex` bitmaps with a designed fallback), with the Winamp query grammar driving saved views.

## Tasks
- [ ] Library window chrome: `gen.bmp` tiled titlebar/borders (25px segments) + `genex.bmp` widgets + its variable-width font; RGB(0,198,255) marker-color tiling handled; designed fallback layout when a skin omits gen files.
- [ ] `genex` color swatches applied to listview/edit/button/scrollbar (the y=0 x=48+ pixel columns — research table).
- [ ] Artist → Album → Tracks tree (data from LI-2's GROUP BY queries, lazy-loaded nodes).
- [ ] Track grid: columns (title/time/artist/album/rating/playcount/lastplayed), sort, inline rating stars (click), skinned scrollbar.
- [ ] Filters bar (genre/year/artist dropdowns).
- [ ] Query-grammar parser → SQL: operators `= != < > HAS NOTHAS LIKE BEGINS ENDS ISEMPTY`, `&& || !`, relative dates `[1 week ago]`; syntax errors surface inline (not crashes).
- [ ] Default smart views (recently added/played/modified, most played, never played, top rated) as saved queries; user-created views persist.
- [ ] Search box with `?`/`query:` prefix behavior (Winamp semantics).
- [ ] Rating + playcount/lastplayed update hooks fire on playback events (scheduler → library write, async).

## Acceptance
- Grammar unit tests: every operator, combined expressions, malformed inputs (error, not crash); relative-date fixtures (clock-mocked).
- Default smart views return identical rows to hand-written reference SQL on the 100k synthetic library.
- Tree + grid browse 100k tracks: scroll 60fps, node expand < 100ms p95.
- Rating a track from the grid writes the DB and the star persists across restart; playcount/lastplayed tick after playback.
- Library window renders correctly for corpus skins *without* gen files (fallback) and *with* (5 fixtures).

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
