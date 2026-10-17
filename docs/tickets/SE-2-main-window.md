# SE-2 · Main window full control set

- **Milestone:** M1 · **Est:** 7d · **Depends:** SE-1, FE-4 · **Status:** in-review

## Story
The main player window is the product: every control of classic Winamp's 275×116 surface rendered from the skin, wired to real playback, and pixel-indistinguishable from Winamp/Webamp in every state. This is the largest single skin-engine ticket and the template the EQ (SE-5) and playlist (SE-3) windows follow.

## Tasks
- [x] Titlebar: active/inactive/shade sprite rows; menu button, minimize/shade/close buttons, clutterbar (`O`/`A`/`I`/`D` toggles). — `src/ui/PlayerRenderer.cpp` (`MainLayout`/`ShadeLayout`), `SkinHitRegion.cpp`.
- [x] Posbar: groove + 29px slider, drag-to-seek with sprite hover/active states, click-to-seek. — `MAIN_POSITION_SLIDER_THUMB[_SELECTED]`, `thumbLeft(...) == round(pct·219)`.
- [x] Volume + balance: 28-frame volume bar, 27-frame balance (mono-center snap), drag + mouse-wheel, `balance.bmp` off-by-9 groove. — volume sprite `round(pct·28)`, 15px stride; balance `floor(|bal|·27/100)`, sheet x=9; a 2px-wide centre detent (`kBalanceCentreDetent`).
- [x] Shuffle/repeat toggles (4 states each, 47px overlap quirk).
- [x] Transport: prev/play/pause/stop/next/eject sprites + events (eject = file-open dialog).
- [x] `monoster` stereo/mono indicators (2px overlap rule). — the "selected" sprite is only used while a track is loaded (Webamp's `.stop #stereo.selected` rule).
- [x] KBPS/KHZ text fields (bitrate/rate from engine). — `khz` from the PCM tap's `PcmFormat.sampleRate`, `kbps` renders Winamp's `"  0"` placeholder: `IAudioEngine` carries no media-info query (see Deviations).
- [x] Time display: `numbers.bmp` digits (minus from `nums_ex`), elapsed/remaining toggle, leading-zero handling. — digits `9x13` at x=48/60/78/90; the countdown minus is borrowed from digit 2's midline (`NUMBERS` 5x1) or the `NUMS_EX` glyph.
- [x] Song title: `text.bmp` 5×6 bitmap-font renderer (uppercase-only set + spec'd fallbacks), marquee scroll. — `src/ui/TitleText.cpp`, Webamp's 190-entry diacritic table; ≥31 cells doubles with `"  ***  "` and scrolls one cell per 220 ms.
- [x] Spectrum + oscilloscope: 75 bars/points from PcmTap FFT, `viscolor.txt` colors (#2-17 spectrum, #18-22 osc), Winamp decay curve; vis-mode click cycles. — `src/ui/VisData.cpp` (1024-pt Winamp transform, 75 bands, dust/peaks); the cycle is spectrum → oscilloscope → off, matching Webamp/2.x (`Pb = ["BAR","OSCILLOSCOPE","NONE"]`), see Deviations.
- [x] Per-state golden screenshots: normal/shade × shuffle on/off × mono/stereo × playing/paused/stopped × 4 slider positions. — 41 states × {1×, 2×} in `src/ui/GoldenStates.cpp`, hashes locked in `tests/fixtures/se2/goldens.sha256`, PNGs written by `spike_fe3 --all <dir>`.
- [x] Interaction-parity checklist (from observed Winamp behavior, e.g. seek during pause, click vs drag on sliders) — 100%. — 62 scripted items in `MainWindow::interactionChecklist()`, each exercised in `tests/se2_interaction_test.cpp`; the suite asserts the exercised set covers the checklist exactly.

## Acceptance
- Golden screenshots (1× and 2× integer scale) diff ≤ 0.1% per state vs baselines — baselines rendered by *us* offscreen and by Webamp for the initial fixture set.
- Interaction parity checklist passes 100% (each item is a scripted input + expected sprite/state assertion).
- 60fps animation of spectrum + marquee on commodity hardware (Ryzen iGPU class) with ≤ 5% CPU for the paint path.

## Out of scope
- EQ window (SE-5), playlist window (SE-3) — only the EQ/PL *toggle buttons* in shufrep.
- Snapping/docking (SE-4) — window is movable but unsnapped here.
- Double-size mode quirks beyond integer 2× (fractional scaling policy is spec §5.2, global).
- Media-library/gen windows.

## References
- `spec.md` §5.1-5.2 — sprite set, rendering rules, integer scaling
- `docs/research/skins.md` §1.1-1.4 — file/sprite geometry tables incl. all overlap quirks (§1.4) and RLE8 gotchas
- Webamp `skinSprites.ts` (MIT) — the ported coordinate table (SE-1 deliverable); behavior reference for decay/marquee timing
- `docs/research/formats.md` §5 — the 75-band truth (576 is capacity)

## Notes / risks
- The overlap quirks (shuffle/repeat 1px, mono-over-stereo 2px, active-shade-row == inactive-top) are invisible until they're wrong; they're enumerated in skins.md §1.4 — check each off explicitly against the golden shots.
- Spectrum decay constants: derive from observed Winamp behavior + Webamp's implementation (MIT — read and adapt), then lock into the spec table.

---

## Evidence (SE-2)

### Delivered
- `src/ui/` — `PlayerRenderer` (the whole control set as one pure `(Skin, VisColor, PlayerState) →
  QImage` function), `PlayerState` + `GoldenStates`, `SkinCanvas` (integer-scale painting with a
  cached chrome + live vis overlay), `SkinnedWindow` (frameless, `region.txt` mask, title-bar drag,
  double-click shade), `MainWindow` (state machine, input, ticker, transport wiring), `SkinHitRegion`
  (hot areas + mask), `TitleText` (5×6 font + marquee), `VisData`/`VisFeed` (transform, 75 bands,
  oscilloscope, lock-free PCM ring), `Transport`/`PlaybackCoreTransport` (the PlaybackCore bridge).
- `spikes/fe3_wsz/` — the renderer swap: `MainWindowRenderer.{cpp,hpp}` deleted, the spike links
  `koalaamp_ui` and renders through `ui::renderMainWindow`; `capture.mjs` grew a 15-state Webamp
  matrix which it reports as JSON (`reference-state.json`); `render-and-diff.sh` diffs the whole
  matrix from that JSON.
- Tests: `tests/se2_render_test.cpp` (11 cases), `tests/se2_interaction_test.cpp` (62-item
  checklist), `tests/se2_corpus_render_test.cpp` (30-skin smoke), `tests/fe3_wsz_test.cpp` updated
  to the app renderer.

### Webamp pixel diff (`spikes/fe3_wsz/render-and-diff.sh`)
```
diff: 0/31900 ... # default          (0.0000%)
diff: 0/31900 ... # toggled          (0.0000%)
diff: 0/3850  ... # shade            (275x14)
diff: 0/3850  ... # shade_play
diff: 0/31900 ... # playing          (silent stream: viscolour bg + dither, no bars)
diff: 0/31900 ... # paused
diff: 0/31016 ... # vol0             (volume rect masked — see Deviations)
diff: 0/31900 ... # vol100
diff: 0/31900 ... # bal_left
diff: 0/31900 ... # bal_right
diff: 0/31900 ... # pos_half
diff: 0/31900 ... # pos_max
diff: 0/31900 ... # playing_mono
diff: 0/31900 ... # vis_off
diff: 0/31900 ... # scroll           (marquee mid-scroll, offset 155 px)
```
All states are rendered from the state Webamp itself reported (status/volume/balance/position/
shuffle/repeat/channels/time/kbps/khz/title/marquee offset/vis mode), so the comparison is
like-for-like rather than table-vs-table. The FE-3 regression numbers (default/toggled) are
unchanged by the renderer swap.

### Golden frames
`build/linux-gcc/spikes/fe3_wsz/spike_fe3 --skin third_party/skins/base-2.91.wsz --goldens`
emits 41 states × {1×, 2×} = 82 SHA-256 digests; `tests/fixtures/se2/goldens.sha256` pins them and
`koalaamp_se2_render_tests` fails on any drift (and on a stale manifest entry). The hashes are over
normalised RGB triples, so they are platform-neutral; the PNGs themselves are Nullsoft artwork and
are never committed (`spike_fe3 --all <dir>` writes them locally).

### Paint-path budget (acceptance item 3)
`spike_fe3 --bench 600` times the two costs the animation loop pays — a full chrome
re-composition (only when the state changes; the marquee steps ~4.5x/s) and a cached-chrome
blit plus the visualiser overlay (every 16 ms tick) — plus the per-tick FFT:
```
bench (600 frames): chrome=0.123 ms  frame=0.104 ms  fft=0.018 ms
bench: 60 fps -> 7.880 ms/s = 0.79% of one core
```
Measured on the reference box (i7-13700, RelWithDebInfo); the budget is 5 % of one core for the
paint path, so there is ~6x headroom. The animation ticker stops entirely while stopped.

### Recorded constants
- Vis transform: FFT size 1024 → 512 bins; envelope `(0.5+0.5·sin(2πi/N − π/2))^1`; equalise
  `log10(1+a+(i+1)·t)` with `a` starting at 0.04 and decaying ×1/1.0025, `t` recomputed from the
  current `a` each iteration; bands `f = log10(512)·i/74`, bin `i/74·511·0.09 + 0.91·10^f`.
- Decay: `saFalloff -= 12/16` per frame ("moderate"), peak falloff ×1.1 ("slow"), bars capped at 15,
  `pushDown = 2` at 1× — so a bar of height `h` paints rows `16−h … 15` of the 16-row viscolor
  gradient and the peak marker sits at `16 − (peak+1−2)` in viscolor[23].
- Visualiser band width: `wide` (4-sample average, every 4th column unpainted) — Winamp/Webamp default.
- Marquee: one cell (5 px) per 220 ms, `period = 5·(cells + 7)`, static below 31 cells.
- Time display: digits 9×13 at x = 48/60/78/90, y = 26; countdown minus 5×1 at (38, 32).
- Volume frame `(round(pct·28)−1)·15` (clamped to ≥0), balance frame `floor(|pan|·27/100)·15`,
  thumbs `round(fraction·(track−thumb))` with tracks 65/38/248 px and thumbs 14/14/29 px.

### Deviations (recorded, deliberate)
1. **`vol0` vs Webamp (0.63 % of that frame, volume rect only).** Webamp's volume
   `background-position` becomes `0 --15px` at volume 0/1 — invalid CSS, so it keeps the previous
   frame while the thumb moves. We clamp to the strip's first frame (what Winamp draws), so the
   harness masks `107,57,68,13` for that one state instead of copying the bug.
2. **`kbps` placeholder.** `IAudioEngine` exposes no media-info query, and SE-2 must not touch
   `src/playback` or `src/interop` (AU-1 owns the PCM seam), so `PlaybackCoreTransport::bitrateKbps()`
   returns 0 and the field renders Winamp's `"  0"` — exactly what an untagged file shows. Wiring it
   needs an engine API addition (AU-side).
3. **`balance` panning is modelled, not applied.** `IAudioEngine` has volume only; the window keeps
   the pan in its state (and the skin's layout record) for the sink ticket to consume.
4. **Visualiser cycle is 3-state** (spectrum → oscilloscope → off), matching Webamp and Winamp 2.x;
   the 4-state "both" cycle is a Winamp 5 addition and out of v1 scope.
5. **`nums_ex.bmp` minus sign.** Webamp's CSS puts `NO_MINUS_SIGN_EX`/`MINUS_SIGN_EX` (9×13) at the
   5×1 `#minus-sign` box, which draws outside the time field. We place the 9×13 glyph in the free
   9-px slot at (39, 26) — the only placement that stays inside the display. Not covered by a Webamp
   diff: the fixture skin has no `nums_ex.bmp`.
6. **Title text source.** SE-2 renders the media's display name (filename stem) or the "KoalaAmp"
   user message; ATF/tag formatting is SE-3's.
7. Dialog-opened surfaces (Winamp's main menu, file-info, milkdrop) emit signals
   (`menuRequested`/`fileInfoRequested`/`visualizerToggled`) — the dialogs themselves are other
   tickets; the buttons, their sprites and their click handling are complete here.

### Clean-room
No WCL/GPL source was consulted. The added behaviour is ported from Webamp (MIT) at the pinned
commit — `css/main-window.css`, `js/skinSelectors.ts`, `js/visualizer/FFT.ts`,
`js/visualizer/Visualizer.ts`, `js/components/Marquee.tsx`, `js/utils.ts` — with attribution in the
file headers and `LICENSES.md`. The sprite table itself is SE-1's.

## Post-review fixes
- **The player now follows the engine.** `PlaybackCoreTransport` already emitted `stateChanged` / `trackChanged` / `positionChanged` / `volumeChanged`, but nothing was connected to them, and `syncFromTransport()` stopped short of its own contract ("...and repaints"). The animation timer — the only other poller — is gated on the status it is meant to keep current, so a track that began playing asynchronously never produced a time or a position: the progress bar stayed empty and the clock dark. The host now connects the four signals to both windows' `syncFromTransport()`, which re-evaluates the tick timer and repaints.
