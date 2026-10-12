# SE-5 · Equalizer window (classic)

- **Milestone:** M1 · **Est:** 4d · **Depends:** SE-1, SE-4, AU-1 · **Status:** unstarted

## Story
The third classic window: the full EQ surface from `eqmain.bmp` (275×315: 11 slider grooves, 61-frame thumbs, ON/AUTO/RESET buttons, preset panel) and `eq_ex.bmp` windowshade (275×54) — bound live to AU-1's `Equalizer` so moving a slider is audible within one audio block, with glitch-free preset switching.

## REUSE, DON'T REIMPLEMENT
- **AU-1's DSP is the ONLY EQ implementation** (`src/audio/`): `EqSettings`/`Equalizer` with the 0–63 slider scale (31 = 0 dB), `EqMode` (Winamp-curve vs Constant-Q), coefficient crossfade on change, `AlbumEqStore` for auto-load, `EqPreset` (.eqf read/write, `winamp.q1` import, 17 classic presets). The window *renders and routes*; it never computes gains.
- **SE-2's window/render patterns** (`src/ui/`): same architecture — pure-function renderer, `SkinnedWindow`, `SkinHitRegion`, parity-suite style. The main-window's volume/balance sliders are the drag/wheel/keyboard pattern to port (28-frame volume vs 61-frame EQ thumbs — the mechanics, not the constants).
- **SE-1's loader** already carries `eqmain.bmp`/`eq_ex.bmp` in the sprite table (with FE-3's **corrected** geometry: `eq_ex` is 275×82 per the real file, not the published 275×54 — verify against the loaded sheet, use actual sheet metrics).
- **FE-4's transport** for the enabled/on-persisted state (via `ITransport`/observer; the EQ-enabled flag rides the skin-DB/per-app state, NOT the engine).

## Tasks
- [ ] `EqRenderer` (pure function, Skin + EqWindowState + EqSettings → QImage): titlebar, ON/AUTO/RESET button sprites (each 2–4 states), 10 band grooves + preamp groove, 61-frame slider thumbs at per-band positions, preset list panel (title + prev/next + load/save buttons), `eq_ex` shade bar with its slider previews.
- [ ] `EqWindowState`: 11 slider values (0–63, preamp included), enabled/on, auto-load indicator, preset index, shade.
- [ ] `EqWindow` (`SkinnedWindow`, `src/ui/`): slider widgets (drag + wheel + keyboard, groove/thumb geometry per sprite table), ON toggle (bypass — passes settings to AU-1 with enabled=false), AUTO (per-album auto-load via `AlbumEqStore`), RESET (all 31), preset panel interactions (load user/named presets, save via `.eqf` dialog, import `winamp.q1`).
- [ ] Wiring to `src/audio`: slider → `EqSettings` update → audible within one 1024-frame block (the AU-1 crossfade guarantees no click — assert in the parity suite via a PCM tap fixture); preset switch glitch-free (same assertion); windowshade preview updates.
- [ ] `.eqf` save/load from this window's dialogs (AU-1's codec, byte-compatible round-trip already tested — the window adds the file dialog and error surfacing).
- [ ] Golden screenshots ×{1×,2×}: normal/shade × on/off × auto × 4 representative slider positions (incl. preamp extremes) × preset-selected — SHA-256 manifest, hashes only.
- [ ] Interaction-parity suite (SE-2 style) + corpus render smoke (30/30 eqmain renders).

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
