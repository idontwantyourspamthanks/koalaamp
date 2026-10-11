# FE-3 · Spike: WSZ loader renders base-2.91

- **Milestone:** M0 · **Est:** 2d · **Depends:** FE-0 · **Status:** in-review

## Story
Retire the biggest unknown in the classic-skin path before committing to the full
SE-1 loader: prove that a ZIP (`.wsz`) can be opened, its bitmaps decoded
(including the RLE8 sheets Nullsoft actually ships), and the main window
composed from the canonical sprite table so that it is **pixel-identical to
Webamp's render of the same skin**. This is the gate SE-1 and SE-2 depend on.

## Tasks
- [x] ZIP open + read (stored/deflate), case-insensitive basename lookup.
- [x] BMP decode: BI_RGB (1/4/8/24/32) + BI_RLE8 (runs, absolute, delta, EOL/EOB, padding).
- [x] Port Webamp `skinSprites.ts` coordinate table to C++ (`SkinSprites.hpp`).
- [x] QPainter main-window renderer: main / titlebar / clutterbar / cbuttons /
      posbar / volume / balance / shufrep / monoster / playpaus / numbers /
      text font; nearest-neighbour, no antialiasing.
- [x] Headless Webamp reference render (independent) at 1×; pixel diff ≤ 0.1 %.
- [x] Verify button-state toggles (shuffle/repeat selected, play held = `_ACTIVE`).
- [x] Fetch script for the gitignored `base-2.91.wsz` fixture (MD5-pinned).
- [x] Catch2 test for the decoder + sprite table (fixture-dependent cases skip).
- [x] Spike report `docs/spikes/fe3-wsz-render.md`.

## Acceptance / Definition of Done
- Main window pixel-diffs **≤ 0.1 %** vs Webamp's render of `base-2.91.wsz` at 1×
  → **0/31 900 (0.0000 %)** for both the default and toggled states.
  Command: `spikes/fe3_wsz/render-and-diff.sh`.
- Button states toggle → shuffle/repeat/play-active captured and diffed at 0 %.
- Stable artifacts → `koalaamp_fe3_tests` (7 cases, `ctest -R fe3`): synthetic
  BMP decode (incl. RLE8 absolute/delta), sprite-table coordinates, and a
  fixture-backed load+render when `base-2.91.wsz` is present.
- Report written, decision recorded, SE-1 handoff (`SkinSprites.hpp`) documented.

## Out of scope
- The full 30-skin golden corpus, folder skins, drag-drop, skin DB (SE-1).
- Interaction/behavior wiring, marquee scrolling, analyser spectrum (SE-2).
- `region.txt` window shaping (FE-1 / SE-4).
- EQ/playlist window rendering (SE-5 / SE-3) — their sprites are ported but not
  composed.
- Time digits/`kbps`/`khz` have a code path but no captured reference yet.

## References
- `spec.md` §5.1–5.2 — canonical bitmap set, rendering rules (nearest-neighbour)
- `docs/research/skins.md` §1.1–1.4 — file set, sizes, RLE8 gotcha, overlaps
- Webamp (MIT) `packages/webamp/js/skinSprites.ts`, `css/main-window.css`,
  `js/skinSelectors.ts` @ `e6283c0e` — the ported table and layout
- `docs/spikes/fe3-wsz-render.md` — this spike's report
- Implementation: `spikes/fe3_wsz/` (`spike_fe3`, `SkinSprites.hpp`, `diff.py`,
  `reference/capture.mjs`), `tests/fe3_wsz_test.cpp`
