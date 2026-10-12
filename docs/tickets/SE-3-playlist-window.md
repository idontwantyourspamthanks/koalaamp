# SE-3 · Playlist editor (classic)

- **Milestone:** M1 · **Est:** 6d · **Depends:** SE-1, FE-4 · **Status:** in-review

## Story
The classic playlist editor window: `pledit.bmp` + `pledit.txt` theming, the segment-tiling resize model, drag-reorder, J jump dialog with the real queue, and ATF title formatting — the second skinned window, built on SE-2's proven patterns and consuming FE-4's PlaybackCore queue directly.

## REUSE, DON'T REIMPLEMENT
- **SE-2's window/render patterns are the template** (`src/ui/`): `SkinnedWindow`/`SkinCanvas`/renderer-as-pure-function/`SkinHitRegion`/`PlayerState`-style state/interaction-parity-suite style. Copy the architecture, don't invent a second one.
- **SE-1's loader already parses `pledit.txt`** (`src/skins/SkinText` — Normal/Current/NormalBG/SelectedBG/Font keys) and `pledit.bmp` is in the sprite table with its tile geometry.
- **FE-4's queue is real**: `PlaybackCore` has `enqueue/clearQueue/queueSize/peekNext` + Winamp prev-restart semantics + repeat/shuffle modes. Drive it through the existing `ITransport` seam — do NOT modify `src/playback` or `src/interop` (concurrent tickets own those surfaces).
- AU-1's `AlbumEqStore` pattern shows how a window persists per-context state via `src/skins/SkinDb`.

## Tasks
- [x] `PleditRenderer` (pure function, Skin + PleditColors + PleditState → QImage): titlebar tiles, list background, track rows (font from `pledit.txt` or the 5×6 fallback), selected/current/queued/highlight row states, scrollbar, the classic resize grip and windowshade bar (25×29-segment tiling per Webamp constants — `WINDOW_RESIZE_SEGMENT_WIDTH=25`).
- [x] `PleditState`: entries (title, length, queue position), selection model (single/ctrl/shift), current index, scroll, shade state.
- [x] `PlaylistWindow` (`SkinnedWindow` subclass, `src/ui/`): segment-tiling resize (grip drag, minimum 275×116-ish per Winamp), drag-reorder rows (internal move + queue reorder semantics), multi-select, right-click verb menu (play/enqueue/remove/rating→stub/misc→jump), J jump dialog (fuzzy match over entries, queue column, move-in-queue/clear-queue), scrollbars (classic sprites), windowshade (1-line bar).
- [x] Queue semantics through `PlaybackCore`: enqueue-highlight rendering (the queued track shows its queue position), queue-consumed-before-shuffle behavior verified via observer events; "load playlist into queue" (JTFE verb) as the enqueue-all action.
- [x] ATF title formatting (spec §11 subset: `%artist% %title% %album%`, `$if2()`, `$filepart()`, default `[%artist% - ]$if2(%title%,$filepart(%filename%))`) — pure function with unit tests; the *tag source* is the file stem only for now (a metadata API is LI-1's; record the seam).
- [x] Golden screenshots: render states × {1×,2×} offscreen (default, selected, current-playing, queued, shaded, several widths) — SHA-256 manifest like SE-2's, hashes only, Nullsoft art never committed.
- [x] Interaction-parity suite in SE-2's style (scripted mouse/key + asserts on transport calls AND render state; fails if any checklist item unexercised).
- [x] Corpus render smoke: all 30 corpus skins' playlist windows render without crash, right sizes, nearest-neighbour 2×.

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


## Evidence

Machine-checked by the SE-3 suites (`ctest`; full run 116/116 green):

- **Renderer / goldens / perf** — `koalaamp_se3_render_tests` (8 cases): 25x29
  segment geometry, row-state colours, 1x/2x sizes (275x116, +25/+29 per
  segment, 14 px shade), exact nearest-neighbour 2x, the committed
  `tests/fixtures/se3/goldens.sha256` (16 states x {1x,2x} = 32 SHA-256
  digests, regenerate with `koalaamp_se3_goldens_gen`), the 5x6 font fallback,
  and the perf floor.
- **10k-row scroll** — the perf case builds a 10 000-entry playlist and renders
  240 scrolling frames at **0.14 ms/frame** (≈0.9 % of a 60 fps budget) on the
  reference Linux box: the renderer is a pure function over the *visible* rows
  only.
- **ATF / model / guard** — `koalaamp_se3_atf_tests` (9 cases): the default
  `[%artist% - ]$if2(%title%,$filepart(%filename%))` incl. the `$if2` fallback
  and `$filepart`, malformed-input totality, the fuzzy jump ranking, selection /
  drag-reorder / queue remap, duration formatting, and the layering guard
  (playback/interop/audio stay UI-free; only `PlaybackCoreTransport` includes
  the engine).
- **Interaction parity** — `koalaamp_se3_interaction_tests`: all **33**
  `PlaylistWindow::interactionChecklist()` items are scripted and the suite
  fails if any is unexercised.
- **Queue semantics** — enqueue-then-play order, drag-reorder remapping the
  queue, `loadIntoQueue` (JTFE), enqueue-highlight positions, and
  queue-consumed-before-refill shifts (the engine pops; the window's view
  follows) are asserted; FE-4's own suite covers the core deque semantics.
  `koalaamp_se3_queue_tests` additionally drives the *real* `PlaybackCore`
  through `PlaybackCoreTransport` (enqueue FIFO, `peekNext` = front, shuffle
  leaves the queue order alone, clear).
- **Corpus** — `koalaamp_se3_corpus_tests`: all 30 corpus skins render every
  state at the right size with exact 2x (30/30).
- **No protected edits** — no changes under `src/playback`, `src/interop` or
  `src/audio`; `ITransport` gained the queue verbs (the sanctioned seam) and
  `PlaybackCoreTransport` bridges them.

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
