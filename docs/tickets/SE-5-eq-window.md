# SE-5 · Equalizer window (classic)

- **Milestone:** M1 · **Est:** 4d · **Depends:** SE-1, SE-4, AU-1 · **Status:** unstarted

## Story
The third classic window: the full EQ surface from `eqmain.bmp` — preamp + 10 band sliders with their 61-frame thumbs, ON/AUTO/RESET buttons, the preset panel, and the `eq_ex.bmp` windowshade — bound live to AU-1's biquads so moving a slider is audible within one audio block.

## Tasks
- [ ] `eqmain.bmp` render: titlebar, ON/AUTO/RESET buttons (sprite states), 11 slider grooves (preamp + 10 bands), 61-frame slider thumbs, preset list panel (title + prev/next + load/save buttons).
- [ ] `eq_ex.bmp` windowshade: 275×54 bar, EQ toggle in shade, slider value previews.
- [ ] Slider widget: drag + mouse-wheel + keyboard (up/down); value 0-64 (31=0dB); groove/thumb geometry per the SE-1 coordinate table.
- [ ] Bind to AU-1: slider → biquad gain within one 1024-frame block; preset switch crossfades coefficients (no click).
- [ ] Preset panel: 18 named presets (Winamp's set) + user presets; load/save via `.eqf` from this window; auto-load-EQ indicator (album match) shows when active.
- [ ] Snap/dock integration with the SE-4 window set (player ↔ EQ ↔ playlist chain, grouped drag).
- [ ] Golden screenshots: normal/shade × on/off × auto × 4 slider positions (incl. preamp extremes); preset-selected state.

## Acceptance
- Golden screenshots diff ≤ 0.1% per state at 1×/2×.
- Slider drag: measured audio change within one block (test taps the pipeline and asserts the biquad gains applied by the next callback).
- Preset switching is glitch-free under audio (tap RMS continuity check).
- `.eqf` saved from this window loads in Webamp/Winamp (fixture round-trip) and vice versa.
- EQ ON toggle A/B: bypass is truly flat (pipeline tap identical to source).

## Out of scope
- EQ algorithms (AU-1); window manager (SE-4 — we consume its snap API).
- ISO-frequency band table option (`eq_frequencies` config) — v2 polish; Winamp bands only in v1.
- Modern-skin EQ (Phase 3).

## References
- `spec.md` §4.2 (semantics), §5.1-5.3 (bitmaps, window model)
- `docs/research/skins.md` §1.2 — `eqmain.bmp` (275×315) / `eq_ex.bmp` (275×54) geometry
- Webamp EQ implementation (MIT) — thumb-frame mapping and preset list behavior reference
- `docs/tickets/AU-1-dsp-eq.md` — the pipeline being driven

## Notes / risks
- The 61-frame thumb strip: frames map to the 0-64 value range *non-linearly* in some skins' conventions — follow Webamp's mapping exactly; don't invent.
