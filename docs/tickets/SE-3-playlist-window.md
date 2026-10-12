# SE-3 · Playlist editor (classic)

- **Milestone:** M1 · **Est:** 6d · **Depends:** SE-1, FE-4 · **Status:** unstarted

## Story
The classic playlist editor window: `pledit.bmp` + `pledit.txt` theming, the segment-tiling resize model, drag-reorder, J jump dialog with the real queue, and ATF title formatting — the second skinned window, built on SE-2's proven patterns and consuming FE-4's PlaybackCore queue directly.

## REUSE, DON'T REIMPLEMENT
- **SE-2's window/render patterns are the template** (`src/ui/`): `SkinnedWindow`/`SkinCanvas`/renderer-as-pure-function/`SkinHitRegion`/`PlayerState`-style state/interaction-parity-suite style. Copy the architecture, don't invent a second one.
- **SE-1's loader already parses `pledit.txt`** (`src/skins/SkinText` — Normal/Current/NormalBG/SelectedBG/Font keys) and `pledit.bmp` is in the sprite table with its tile geometry.
- **FE-4's queue is real**: `PlaybackCore` has `enqueue/clearQueue/queueSize/peekNext` + Winamp prev-restart semantics + repeat/shuffle modes. Drive it through the existing `ITransport` seam — do NOT modify `src/playback` or `src/interop` (concurrent tickets own those surfaces).
- AU-1's `AlbumEqStore` pattern shows how a window persists per-context state via `src/skins/SkinDb`.

## Tasks
- [ ] `PleditRenderer` (pure function, Skin + PleditColors + PleditState → QImage): titlebar tiles, list background, track rows (font from `pledit.txt` or the 5×6 fallback), selected/current/queued/highlight row states, scrollbar, the classic resize grip and windowshade bar (25×29-segment tiling per Webamp constants — `WINDOW_RESIZE_SEGMENT_WIDTH=25`).
- [ ] `PleditState`: entries (title, length, queue position), selection model (single/ctrl/shift), current index, scroll, shade state.
- [ ] `PlaylistWindow` (`SkinnedWindow` subclass, `src/ui/`): segment-tiling resize (grip drag, minimum 275×116-ish per Winamp), drag-reorder rows (internal move + queue reorder semantics), multi-select, right-click verb menu (play/enqueue/remove/rating→stub/misc→jump), J jump dialog (fuzzy match over entries, queue column, move-in-queue/clear-queue), scrollbars (classic sprites), windowshade (1-line bar).
- [ ] Queue semantics through `PlaybackCore`: enqueue-highlight rendering (the queued track shows its queue position), queue-consumed-before-shuffle behavior verified via observer events; "load playlist into queue" (JTFE verb) as the enqueue-all action.
- [ ] ATF title formatting (spec §11 subset: `%artist% %title% %album%`, `$if2()`, `$filepart()`, default `[%artist% - ]$if2(%title%,$filepart(%filename%))`) — pure function with unit tests; the *tag source* is the file stem only for now (a metadata API is LI-1's; record the seam).
- [ ] Golden screenshots: render states × {1×,2×} offscreen (default, selected, current-playing, queued, shaded, several widths) — SHA-256 manifest like SE-2's, hashes only, Nullsoft art never committed.
- [ ] Interaction-parity suite in SE-2's style (scripted mouse/key + asserts on transport calls AND render state; fails if any checklist item unexercised).
- [ ] Corpus render smoke: all 30 corpus skins' playlist windows render without crash, right sizes, nearest-neighbour 2×.

## Acceptance
- 10k-entry list scrolls at 60fps on commodity hardware (measure, report the number; ≤5% CPU for the paint path like SE-2).
- Queue semantics pass behavioral tests: enqueue-then-play order, queue consumed before shuffle refill, dequeue/remove updates the queue positions, JTFE load-into-queue.
- Golden screenshot manifest committed; drift fails the suite; corpus smoke 30/30.
- ATF renders the default titlefmt correctly on fixture filenames (unit tests incl. `$if2` fallback and `$filepart`).
- ctest suite stays green (99/99 at time of writing) + new cases; no changes under `src/playback`/`src/interop`/`src/audio` (grep-asserted like SE-2's guard).

## Out of scope
- Playlist *persistence* (PL-1 owns M3U/PLS read/write — this window consumes an in-memory model).
- Media-library drag-drop INTO the playlist (source doesn't exist yet; record the seam).
- The send-to/rating write-back verbs beyond menu presence.
- EQ window (SE-5), window snapping (SE-4 — this window is standalone-movable but unsnapped).

## References
- `docs/spec.md` §5.4 — playlist window design; §11 ATF subset
- `src/ui/` (SE-2) — the window/render/parity patterns to copy; `tests/se2_*` — the test architecture to copy
- `src/skins/SkinText` — the `pledit.txt` parser (already landed); `SkinSprites` — `pledit.bmp` tiles
- `src/playback/PlaybackCore.hpp` — the queue/transport/observer surface to drive (via `ITransport`)
- `docs/research/skins.md` §1.2-1.4 — pledit geometry, tiling constants, quirks
- Webamp playlist implementation (MIT, pinned e6283c0e) — behaviour reference for tiling/queue/selection semantics

## Notes / risks
- The 25px tiling segments are only two-of-six seamless columns (skins.md §1.4) — the golden screenshots must catch wrong-column tiling.
- Scroll perf at 10k rows: the renderer is a pure function over visible rows — don't render the full list offscreen.
- `pledit.txt` Font= names a *system* font; classic behavior falls back to the 5×6 skin font when it's absent/unavailable — match that (and record that font availability differs per platform, hence the fallback must be tested).
