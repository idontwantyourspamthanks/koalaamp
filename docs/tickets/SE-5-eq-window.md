# SE-5 · Equalizer window (classic)

- **Milestone:** M1 · **Est:** 4d · **Depends:** SE-1, SE-4, AU-1 · **Status:** in-review

## Story
The third classic window: the full EQ surface from `eqmain.bmp` (275×315: 11 slider grooves, 61-frame thumbs, ON/AUTO/RESET buttons, preset panel) and `eq_ex.bmp` windowshade (275×54) — bound live to AU-1's `Equalizer` so moving a slider is audible within one audio block, with glitch-free preset switching.

## REUSE, DON'T REIMPLEMENT
- **AU-1's DSP is the ONLY EQ implementation** (`src/audio/`): `EqSettings`/`Equalizer` with the 0–63 slider scale (31 = 0 dB), `EqMode` (Winamp-curve vs Constant-Q), coefficient crossfade on change, `AlbumEqStore` for auto-load, `EqPreset` (.eqf read/write, `winamp.q1` import, 17 classic presets). The window *renders and routes*; it never computes gains.
- **SE-2's window/render patterns** (`src/ui/`): same architecture — pure-function renderer, `SkinnedWindow`, `SkinHitRegion`, parity-suite style. The main-window's volume/balance sliders are the drag/wheel/keyboard pattern to port (28-frame volume vs 61-frame EQ thumbs — the mechanics, not the constants).
- **SE-1's loader** already carries `eqmain.bmp`/`eq_ex.bmp` in the sprite table (with FE-3's **corrected** geometry: `eq_ex` is 275×82 per the real file, not the published 275×54 — verify against the loaded sheet, use actual sheet metrics).
- **FE-4's transport** for the enabled/on-persisted state (via `ITransport`/observer; the EQ-enabled flag rides the skin-DB/per-app state, NOT the engine).

## Tasks
- [x] `EqRenderer` (pure function, Skin + EqWindowState + EqSettings → QImage): titlebar, ON/AUTO/RESET button sprites (each 2–4 states), 10 band grooves + preamp groove, 61-frame slider thumbs at per-band positions, preset list panel (title + prev/next + load/save buttons), `eq_ex` shade bar with its slider previews.
- [x] `EqWindowState`: 11 slider values (0–63, preamp included), enabled/on, auto-load indicator, preset index, shade.
- [x] `EqWindow` (`SkinnedWindow`, `src/ui/`): slider widgets (drag + wheel + keyboard, groove/thumb geometry per sprite table), ON toggle (bypass — passes settings to AU-1 with enabled=false), AUTO (per-album auto-load via `AlbumEqStore`), RESET (all 31), preset panel interactions (load user/named presets, save via `.eqf` dialog, import `winamp.q1`).
- [x] Wiring to `src/audio`: slider → `EqSettings` update → audible within one 1024-frame block (the AU-1 crossfade guarantees no click — assert in the parity suite via a PCM tap fixture); preset switch glitch-free (same assertion); windowshade preview updates.
- [x] `.eqf` save/load from this window's dialogs (AU-1's codec, byte-compatible round-trip already tested — the window adds the file dialog and error surfacing).
- [x] Golden screenshots ×{1×,2×}: normal/shade × on/off × auto × 4 representative slider positions (incl. preamp extremes) × preset-selected — SHA-256 manifest, hashes only.
- [x] Interaction-parity suite (SE-2 style) + corpus render smoke (30/30 eqmain renders).

## Acceptance
- Golden screenshots diff ≤0.1% per state at 1×/2×; corpus 30/30.
- Slider drag: the DSP's applied gains change within one audio block (asserted via a PCM tap — the AU-1 test pattern).
- Preset switching is glitch-free under audio (RMS continuity check — AU-1's crossfade test generalizes).
- `.eqf` saved from this window loads in Webamp (fixture round-trip) and vice versa.
- EQ ON bypass is truly flat: tap identical to source (AU-1's existing flat-assertion reused).
- ctest green (99/99 baseline + new); no changes under `src/playback`/`src/interop` (grep-asserted).

## Out of scope
- EQ algorithms (AU-1 owns all DSP); ISO-frequency band option (v2 polish); auto-load *matching rules* beyond AU-1's album-key store.
- Window snapping (SE-4 — standalone-movable here; note SE-4 may not have landed when you start: use SE-2's standalone-movable pattern and leave the snap seam documented).
- Modern-skin EQ (Phase 3).

## References
- `docs/tickets/AU-1-dsp-eq.md` + `src/audio/` — the API you drive (`EqSettings`, `Equalizer`, `EqPreset`, `AlbumEqStore`) and its test patterns
- `src/ui/` (SE-2) — window/render/parity patterns; `tests/se2_*` — test architecture
- `docs/tickets/SE-2-main-window.md` — the volume/balance slider interaction spec (61-frame EQ thumbs use the same mechanics)
- `docs/research/skins.md` §1.2 — eqmain/eq_ex geometry **incl. the FE-3 correction (eq_ex 275×82 measured)**; the 61-frame thumb-strip mapping
- Webamp EQ implementation (MIT, e6283c0e) — thumb-frame mapping and preset-panel behavior reference (the ticket's earlier warning: the mapping is non-obvious in spots; follow Webamp exactly)

## Notes / risks
- **SE-4 dependency is soft**: if SE-4 hasn't landed, build standalone-movable (SE-2 pattern) and leave a documented snap seam — do NOT implement snapping here.
- The 61-frame thumb strip: frames map to the 0–63 value range non-obviously — port Webamp's mapping, verify against golden screenshots.
- AU-1's slider scale is 0–63 (63 = −12 dB, 31 = 0 dB, 0 = +12 dB — INVERTED from intuition); the window must not silently flip it.

## Evidence (in-review)

All commands run in the ticket's isolated worktree, `cmake --preset linux-gcc`,
`QT_QPA_PLATFORM=offscreen` where a platform plugin is needed.

### Render parity with Webamp (the strongest check)
`spikes/se5_eq/render-and-diff.sh` renders `koalaamp::ui::renderEqWindow` for the
golden states and diffs them against Webamp 2.3.1's own equalizer window in
headless Chrome, driven to the same slider values (`round((63-au)*100/63)`, the
one mapping the renderer uses):

```
eq_default          0/31900 → 0.0000%
eq_on               0/31900 → 0.0000%
eq_bands_maxboost   0/31900 → 0.0000%
eq_bands_maxcut     0/31900 → 0.0000%
eq_preamp_maxboost  0/31900 → 0.0000%
eq_shade            0/3850  → 0.0000%
eq_shade_extremes   0/3850  → 0.0000%
```
Byte-identical on every state — normal window and windowshade.

### Test suites
* `ctest --preset linux-gcc` → **110/110 passed** (baseline 99/99 kept green +
  11 new SE-5 tests).
* `koalaamp_se5_render_tests`: 188 assertions, 5 cases (formulas, 275×116 /
  275×14, per-control confinement, exact integer 2×, committed golden hashes for
  19 states × {1×,2×} in `tests/fixtures/se5/goldens.sha256`).
* `koalaamp_se5_interaction_tests`: 11377 assertions, 5 cases — the full
  `EqWindow::interactionChecklist()` coverage assertion plus the AU-1 wiring:
  slider change audible within one 1024-frame block (+12 dB ±12 % one block
  later, no block-seam discontinuity), preset switching glitch-free (bounded,
  continuous across 5 preset switches), EQ OFF bit-flat (output == source), and
  `.eqf` save/load round-trip through AU-1's codec (bytes identical to
  `writeEqf`, bands/preamp equal after reparse; `winamp.q1` import = 17 presets).
* `koalaamp_se5_corpus_tests`: 30/30 corpus skins render every EQ state at 1×
  and exact-2×, with real art when the skin carries `eqmain.bmp`.

### Guards / boundaries
* `git diff master -- src/audio src/playback src/interop` is empty — the window
  consumes AU-1's API unchanged (no edits to the three protected modules).
* `src/ui` gained a PUBLIC dependency on `koalaamp_dsp` (Qt-free) for the
  `EqSettings`/`EqPreset`/`AlbumEqStore` API; no audio code was copied.
* No new deps; Webamp remains a test-only reference under the gitignored
  `third_party/webamp/`.

## Deviations & findings (recorded, not silently absorbed)

1. **"61-frame slider thumbs" (Story/Tasks) is not what the sheet contains.**
   Measured against the loaded `base-2.91.wsz` (`eqmain.bmp`, 275×315): the
   slider art is a **14×2 grid of 28 groove frames** inside
   `EQ_SLIDER_BACKGROUND` (13,164,209,129), plus **one** 11×11 thumb sprite
   (`EQ_SLIDER_THUMB`, selected variant at (0,176)). That is exactly Webamp's
   mapping — `sprite = round(value/100*27)`, `x=(sprite%14)*15`,
   `y=(sprite/14)*65`, thumb top `floor(51*(1-value/100))` — and the pixel diff
   is 0/31900. The renderer implements that mapping; the "61" in the brief is
   corrected here with the sheet metrics as ground truth (the ticket's own
   "trust the loaded sheet" rule).
2. **RESET.** Classic EQ has ON/AUTO/**PRESETS**; there is no RESET sprite. The
   reset affordances are the `+12 / 0 / -12 dB` label click zones
   (`EqLayout::plus12db/zerodb/minus12db`, Webamp's `#plus12db/#zerodb/#minus12db`):
   the `0 dB` zone sets every band to unity (and `EqWindow::resetAll()` sets the
   preamp too). The checklist covers all three zones as the ticket's "RESET".
3. **Preset panel.** Classic skins ship a single `EQ_PRESETS_BUTTON` (2 states);
   there is no title/prev/next panel. The button opens the request menu (the 17
   classic presets + imported user presets + "From .eqf…" + "Save…"); the same
   operations are exposed as direct API for headless use.
4. **AU→display mapping.** The window stores AU-1's inverted 0–63 field
   verbatim and applies the inversion only in the renderer
   (`eqWebampValue(au) = round((63-au)*100/63)`), so +12 dB is at the top;
   dragging/wheel/keyboard invert back through `eqAuValueFromWebamp`. Webamp's
   own `normalizeEqBand` (1..64) is *not* used — it is a Webamp-side quirk; the
   direct .eqf-byte reading is the faithful one, and rendering-from-a-position is
   byte-identical to Webamp (above).
5. **SE-4 not landed.** The window is standalone-movable (`SkinnedWindow`) and
   emits `dragFinished()` for the window manager; snapping is intentionally not
   implemented.
6. `eq_ex.bmp` is 275×82 (FE-3 correction) but the windowshade is 275×14; only
   the (0,0)/(0,15) bars and the bottom strip's mini-slider sprites are used.
