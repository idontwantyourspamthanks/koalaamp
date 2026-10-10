# SE-2 · Main window full control set

- **Milestone:** M1 · **Est:** 7d · **Depends:** SE-1, FE-4 · **Status:** unstarted

## Story
The main player window is the product: every control of classic Winamp's 275×116 surface rendered from the skin, wired to real playback, and pixel-indistinguishable from Winamp/Webamp in every state. This is the largest single skin-engine ticket and the template the EQ (SE-5) and playlist (SE-3) windows follow.

## Tasks
- [ ] Titlebar: active/inactive/shade sprite rows; menu button, minimize/shade/close buttons, clutterbar (`O`/`A`/`I`/`D` toggles).
- [ ] Posbar: groove + 29px slider, drag-to-seek with sprite hover/active states, click-to-seek.
- [ ] Volume + balance: 28-frame volume bar, 27-frame balance (mono-center snap), drag + mouse-wheel, `balance.bmp` off-by-9 groove.
- [ ] Shuffle/repeat toggles (4 states each, 47px overlap quirk).
- [ ] Transport: prev/play/pause/stop/next/eject sprites + events (eject = file-open dialog).
- [ ] `monoster` stereo/mono indicators (2px overlap rule).
- [ ] KBPS/KHZ text fields (bitrate/rate from engine).
- [ ] Time display: `numbers.bmp` digits (minus from `nums_ex`), elapsed/remaining toggle, leading-zero handling.
- [ ] Song title: `text.bmp` 5×6 bitmap-font renderer (uppercase-only set + spec'd fallbacks), marquee scroll.
- [ ] Spectrum + oscilloscope: 75 bars/points from PcmTap FFT, `viscolor.txt` colors (#2-17 spectrum, #18-22 osc), Winamp decay curve; vis-mode click cycles (spectrum/osc off/both per Winamp behavior).
- [ ] Per-state golden screenshots: normal/shade × shuffle on/off × mono/stereo × playing/paused/stopped × 4 slider positions.
- [ ] Interaction-parity checklist (from observed Winamp behavior, e.g. seek during pause, click vs drag on sliders) — 100%.

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
