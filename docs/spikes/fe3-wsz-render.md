# FE-3 spike — WSZ loader renders base-2.91

- **Status:** in review (2026-10-11) · **Ticket:** FE-3 (`docs/tickets.md`) · **Depends:** FE-0
- **Evidence:** `spikes/fe3_wsz/` (`spike_fe3`, `SkinSprites.hpp`, `diff.py`, `fetch-skin.sh`, `render-and-diff.sh`); reference harness `spikes/fe3_wsz/reference/`; tests `tests/fe3_wsz_test.cpp`
- **Consumes:** Webamp (MIT) `skinSprites.ts` + `css/main-window.css` @ `e6283c0e` — attribution in the file headers and `LICENSES.md`

This spike proves the whole classic-skin render path end to end on the real
Nullsoft `base-2.91.wsz`: ZIP open → RLE8/paletted BMP decode → Webamp's sprite
table blitted with QPainter integer (nearest-neighbour) copies → main window at
1×, pixel-diffed against **Webamp itself** rendered headlessly.

> **SE-1 follow-up (2026-10-11).** The decode core described below
> (`ZipReader`, `BmpDecoder`, `Skin`, `SkinSprites.hpp`) was promoted from this
> spike into `src/skins/` by ticket SE-1; paths quoted in this report are the
> original spike locations. The spike now *links* the promoted modules instead of
> duplicating them, and the §3 pixel-diff has been re-run on that build
> (0/31900, both states).

## 1. Bottom line

1. **The main window matches Webamp exactly: 0 / 31 900 pixels differ
   (0.0000%)** — both the default no-track startup state *and* a toggled state
   (shuffle+repeat selected, play button held down). The whole 275×116 window is
   compared; nothing is masked out (see §3 for why).
2. **The complete Webamp sprite table is ported**: 212 sprites across 15 sheets
   plus the 69-glyph `text.bmp` font — not just the main-window subset, so SE-1
   inherits it unchanged. The only upstream group omitted is `GENEX`
   (commented out in Webamp too).
3. **Our hand-written BMP decoder reproduces Chrome/Skia bit-for-bit on all 18
   sheets.** Qt's own BMP handler (`QImage::loadFromData`) *also* accepts every
   sheet but decodes three of them **differently** (balance/text/gen — see §5),
   which is why SE-1 must ship this decoder rather than lean on Qt.
4. `base-2.91.wsz` is **10 RLE8 + 8 BI_RGB** sheets; several RLE8 streams use
   `delta` runs to skip the spare left columns of `balance.bmp`, and two streams
   (balance, text) are **rejected by Pillow** but accepted by Skia and by us.

## 2. Method

**Fixture** — `spikes/fe3_wsz/fetch-skin.sh` pulls `base-2.91.wsz` (MD5
`5e4f10275dcb1fb211d4a8b4f1bda236`, the Winamp Skin Museum's canonical copy) from
the museum CDN into the gitignored `third_party/skins/`. Nullsoft artwork is
never committed; the script verifies the MD5.

**Our renderer** — `spike_fe3 --skin … --state default|toggled` composes the
window from the decoded sheets (`Skin::blit`, exact rect copies, antialiasing and
smooth-transform disabled). Layout constants come from Webamp's
`css/main-window.css`; sprite rects from the ported `skinSprites.ts`.

**Independent reference** — rather than re-expressing the coordinate table
(which would only prove self-consistency), the reference is a **real Webamp
render**: `spikes/fe3_wsz/reference/capture.mjs` loads the pinned Webamp 2.3.1
bundle in headless Chrome (puppeteer-core, `deviceScaleFactor=1`), mounts the
skin, and screenshots the `#main-window` element at 1×. Two states are captured:

| State | Webamp state applied | PNG |
|---|---|---|
| default | no track → `stop`, volume 78, balance 0, EQ+PL open | `webamp_main_default.png` |
| toggled | shuffle + repeat selected, `#play` held (`winamp-active`) | `webamp_main_toggled.png` |

**Diff** — `spikes/fe3_wsz/diff.py` counts exact RGB mismatches over the window
and compares against the 0.1 % budget.

**Reproduce everything:**
```
cmake --build --preset linux-gcc --target spike_fe3
spikes/fe3_wsz/render-and-diff.sh
```

## 3. Result

```
$ spikes/fe3_wsz/render-and-diff.sh
diff: 0/31900 compared pixels differ = 0.0000% (threshold 0.1000%)   # default
diff: 0/31900 compared pixels differ = 0.0000% (threshold 0.1000%)   # toggled
```

Both states are byte-identical to Webamp's render. **No region is excluded.**
The main window has no live canvas in this configuration: Webamp only creates
its spectrum-analyser canvas when an `AnalyserNode` exists (no audio in the
harness), so the visualiser area falls through to `MAIN.BMP`'s own art — which is
exactly what we blit. Should audio be present, that ~113×19 area becomes
Webamp's live analyser and must be masked out of a future diff.

### Button-state toggles (acceptance: "button states toggle")

The toggled capture exercises the sprite-state machine rather than just the idle
look:

| Control | Webamp class | Sprite (dest) |
|---|---|---|
| shuffle | `.selected` | `MAIN_SHUFFLE_BUTTON_SELECTED` 47×15 @ (164,89) |
| repeat | `.selected` | `MAIN_REPEAT_BUTTON_SELECTED` 28×15 @ (210,89) |
| play | `.winamp-active` (held) | `MAIN_PLAY_BUTTON_ACTIVE` 23×18 @ (39,88) |

All three matched pixel-for-pixel. (Webamp's `##main-window` class is driven by
transport buttons; the idle transport uses `MAIN_*_BUTTON`, the pressed state
`_ACTIVE`.) A one-shot `--state playing` (time digits `01:05` + play indicator +
work indicator + position thumb) is also implemented for spot checks but has no
captured reference yet.

## 4. Sprite-table port status (SE-1 inherits this)

`spikes/fe3_wsz/SkinSprites.hpp` is a mechanical transcription of Webamp
`skinSprites.ts` @ `e6283c0e` (MIT). It is **complete except `GENEX`**
(upstream comments it out pending Media Library support):

| Sheet | Ported | Sheet | Ported |
|---|---|---|---|
| MAIN | 1 | TITLEBAR | 27 |
| CBUTTONS | 12 | VOLUME | 3 |
| BALANCE | 3 | SHUFREP | 16 |
| MONOSTER | 4 | NUMBERS / NUMS_EX | 12 / 12 |
| PLAYPAUS | 5 | EQMAIN | 22 |
| POSBAR | 3 | EQ_EX | 12 |
| PLEDIT | 60 | GEN | 20 |
| TEXT font | 69 glyphs (5×6 cells) | **total** | **212 + 69** |

The 69-glyph `FONT_LOOKUP` is exposed as `fontGlyph(char32_t) -> SpriteRect`.
`SpriteRect`/`SpriteDef` carry a defaulted `operator==`; a Catch2 case asserts
representative entries against the upstream numbers (MAIN 275×116, play button
at (23,0), posbar thumb at (248,0), `MAIN_BALANCE_BACKGROUND` at **x=9**, …).

**Layout** (destinations) is ported from Webamp's `css/main-window.css` into
`MainWindowRenderer.cpp` as named constants. Two subtleties worth carrying into
SE-2:

- The title bar's 275×14 art **already contains the menu/min/shade/close icons**
  for base-2.91; Webamp only overlays the standalone `#option`/`#minimize`/
  `#shade`/`#close` sprites in their *clicked/active* state
  (`skinSelectors.ts`). Drawing the idle sprites double-draws the buttons — our
  first diff was 0.46 % because of exactly this.
- Slider thumbs: Webamp uses range inputs, so the thumb sits at
  `round(fraction * (trackWidth − 14px))`; the volume background frame is
  `(round(vol/100·28) − 1)·15` and the balance frame `floor(|bal|/100·27)·15`.

## 5. BMP / RLE8 findings

- **Sheet formats in `base-2.91.wsz`:** 10 × BI_RLE8 (`main, cbuttons, posbar,
  shufrep, balance, monoster, numbers, text, eqmain, gen`) and 8 × BI_RGB
  uncompressed (`titlebar, volume, playpaus, eq_ex, genex, pledit, mb, video`);
  all 8-bit, all with a 256-entry palette. No 1/4/24/32-bit sheet in this skin —
  the decoder still handles those depths for SE-1's corpus.
- **Header geometry differs from some published tables** (use the header, not
  the doc): `monoster.bmp` is **58×24** (research note said 58×33),
  `text.bmp` is **155×74**, `eq_ex.bmp` **275×82**, `gen.bmp` **194×109**.
  Only the used sub-rectangles (e.g. text rows 0–17) matter.
- **`delta` runs + short streams.** `balance.bmp` uses `00 02 dx dy` delta
  escapes to skip its 9 spare left columns (the documented off-by-9 trap) and
  its stream ends before the declared height; `text.bmp`/`gen.bmp` likewise.
  **Pillow refuses all three** ("not enough image data"). Skia and our decoder
  accept them. **The pixels skipped by a delta must be seeded black** — without
  that, our decode of `balance.bmp` differed from Skia by 11 142 px (uninitialised
  QImage memory). One `image.fill(black)` fixed it; after that, **all 18 sheets
  decode byte-identically to Chrome/Skia**.
- **Qt's BMP handler is not a reference decoder.** `QImage::loadFromData(bmp)`
  returns a valid image for every sheet, but decodes `balance.bmp` (12 570 px),
  `gen.bmp` (6 956 px) and `text.bmp` (448 px) differently from Skia
  (`main.bmp` matches). Consequence for SE-1: ship the explicit decoder
  (`BmpDecoder.cpp`, ~200 LOC) and treat Qt's reader as unusable for RLE8 skins.
- `RLE8` absolute runs cannot encode lengths 0/1/2 (those are EOL/EOB/delta);
  odd-length absolute runs carry a pad byte. The decoder handles both — a
  regression test covers it.

## 6. Decisions recorded

- **Reference = real Webamp in headless Chrome**, not a second rendering of the
  coordinate table. Stronger evidence; needs node + a Chrome binary (both present
  on the reference machine and in CI images).
- **The pixel diff covers the whole window.** No masks are needed because the
  no-audio startup state has no dynamic region (see §3); `diff.py` still accepts
  `--mask` for future audio-bearing captures.
- **Volume kept at Webamp's default (78)** rather than pinned to an extreme; the
  empirically-located thumb (x=147 → offset 40 inside the 65-px track) confirms
  the `round(fraction·(track−14))` formula.
- Renderer core lives in a small static lib (`koalaamp_fe3_wsz`) shared by the
  spike binary and the tests; it is standalone and does not link app code.
- Time digits/`kbps`/`khz` are hidden while stopped (as Webamp does) and a
  `--state playing` path renders them; no reference capture yet — flagged, not
  claimed.

## 7. Deviations / not done

- Windows/macOS rendering not exercised (Linux/X11 reference box only).
- `--state playing` (digits, working indicator, position thumb) is implemented
  but has no independent reference capture; only the default and toggled states
  are asserted at 0 %.
- Only `base-2.91.wsz` was diffed; the broader corpus (RLE8-heavy, 1-bit,
  missing `eq_ex`) is SE-1's job.
- `region.txt` masking is not part of this spike (FE-1/SE-4).

## 8. Clean-room attestation

No WCL/GPL-covered source was consulted. The sprite table and layout constants
are transcribed from Webamp (MIT `skinSprites.ts` / `main-window.css`, pinned
commit) with attribution in the headers and `LICENSES.md`. The ZIP and BMP
decoders are implemented from the public PKWARE/BMP field layouts. The
`base-2.91.wsz` fixture is Nullsoft artwork, fetched locally, gitignored, never
committed.
