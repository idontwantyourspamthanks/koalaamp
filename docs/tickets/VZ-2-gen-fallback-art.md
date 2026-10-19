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

- The `pledit.bmp` rects are already the `PLAYLIST_*` entries in `SkinSprites.hpp` (the same
  table `PleditRenderer` blits). No second table.
- `MAIN_TITLE_BAR[_SELECTED]` in `titlebar.bmp` is a single 275x20 sprite with the option,
  minimize and close buttons drawn into it, so it cannot simply be clipped to a gen window.
  `pledit.bmp`'s sliced title bar is the source.
- The playlist bottom band is 38 px of buttons. The visualizer keeps the gen layout's 14 px
  bottom and uses the band's lower edge: the tile's plain bevel, the left corner join, and
  the resize grip in the right corner's last columns. The title's right piece keeps both of
  the skin's buttons; only the close box is live.
- `text.bmp` titles drop the space-cell background (and a tight dither around it), so the
  name sits on the bar. A cell whose colours sprawl, or a font whose ink would vanish, stays
  opaque.

## Tasks

- [x] Rect table for the `pledit.bmp` chunks in gen-layout terms (title bar left/fill/right,
      side borders, bottom bar), as data, not code. — the existing `PLAYLIST_*` sprites.
- [x] `GenRenderer` composes the frame from it when the skin has the sheet, and keeps VZ-1's
      palette fallback when it has neither `gen.bmp` nor `pledit.bmp`.
- [x] `vz1_viswindow_tests`: a skin with `pledit.bmp` but no `gen.bmp` frames itself in that
      sheet's pixels; a skin with neither still gets the derived palette.
- [x] Render check: gen-less corpus skins (and the four bundled skins, which do ship
      `gen.bmp`) framed and inspected. Title bar runs the full width, the name sits in the
      middle, the skin's buttons sit at the end, and the bottom edge carries the grip.

## Out of scope

- `genex.bmp` (gen window buttons) and `video.bmp`.
- Changing how a skin *with* `gen.bmp` is drawn — that path is VZ-1's and works.
- Removing the palette fallback: skins carrying neither sheet still need it.
