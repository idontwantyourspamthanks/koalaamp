# VZ-2 · Framing a gen-less skin's visualizer from the skin's own art

- **ID / title:** `VZ-2 · Framing a gen-less skin's visualizer from the skin's own art`
- **Depends on:** VZ-1 (the gen frame and its palette-derived fallback)
- **References:** `docs/tickets/VZ-1-visualizer.md`, `docs/research/skins.md` §1.2-1.3,
  `docs/tickets/SE-3-playlist-window.md`

## Story

Most classic skins ship no `gen.bmp` (three of the five corpus skins that have any are the
exception, not the rule), so most users only ever see the visualizer's *fallback* frame. That
fallback is flat fills derived from the skin's `pledit.txt` palette — in the skin's colour
family, but visibly not the skin's art next to the main, EQ and playlist windows, whose chrome
is beveled, gradient art. Two rounds of palette work did not close the gap and cannot: flat
fills are not art.

Compose the fallback from the skin's own chrome art instead. `pledit.bmp` is a complete window
chrome — title bar corners and fill, side borders, bottom bar, close buttons — and
`titlebar.bmp` carries the main window's title bar, active and inactive. Slotting those chunks
into the gen layout makes a gen-less visualizer wear the same art as its siblings.

## Findings so far

- `PleditRenderer` composes the playlist frame from **raw `pledit.bmp` rects**; there is no
  `PLEDIT_*` sprite registry entry (a grep for the name finds only the sheet constant). The rect
  table is therefore new work, sourced from the classic reference rather than from the sprite
  registry.
- `MAIN_TITLE_BAR[_SELECTED]` in `titlebar.bmp` is a single 275x20 sprite with the option,
  minimize and close buttons drawn into it, so it cannot simply be clipped to a 194 px gen
  window. `pledit.bmp`'s sliced title bar is the better source.

## Tasks

- [ ] Rect table for the `pledit.bmp` chunks in gen-layout terms (title bar left/fill/right,
      side borders, bottom bar), as data, not code.
- [ ] `GenRenderer` composes the frame from it when the skin has the sheet, and keeps VZ-1's
      palette fallback when it has neither `gen.bmp` nor `pledit.bmp`.
- [ ] `vz1_viswindow_tests`: a skin with `pledit.bmp` but no `gen.bmp` frames itself in that
      sheet's pixels; a skin with neither still gets the derived palette.
- [ ] Xvfb capture: a gen-less corpus skin's whole set (main, EQ, playlist, visualizer) in one
      skin, all four reading as the same program.

## Out of scope

- `genex.bmp` (gen window buttons) and `video.bmp`.
- Changing how a skin *with* `gen.bmp` is drawn — that path is VZ-1's and works.
- Removing the palette fallback: skins carrying neither sheet still need it.
