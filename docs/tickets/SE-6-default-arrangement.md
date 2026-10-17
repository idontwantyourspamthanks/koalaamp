# SE-6 · Default window arrangement on first run

- **ID / title:** `SE-6 · Default window arrangement on first run`
- **Milestone:** M1 follow-up · **Est:** 0.5d focused · **Depends:** SE-4 (window set, snapping/docking, layout persistence)
- **Status:** in-progress

## Story
A first run — a fresh config, or the first launch of a skin with no saved layout — drops the
player, EQ and playlist windows wherever the compositor puts them, which in practice is the same
coordinates for all three: the app opens as one ambiguous pile, and the docking SE-4 implements is
never *seen* (you cannot drag a window "next to" another when everything is already on top of it).
Winamp opens its set in the classic arrangement — the EQ flush against the player's right edge, the
playlist flush beneath the player. Place the set there when a skin has no saved layout, so the app
opens in the shape users expect and the very first drag of the player carries the whole group.

## Tasks
- [x] `WindowSet::applyDefaultArrangement()`: player at a centred/clamped origin, EQ flush to its
      right, playlist flush below it.
- [x] Call it from `main.cpp` when `restoreLayout()` reports no saved layout, *before* `showAll()`
      (so the group docks as it is shown).
- [x] Test in the window-set suite: the three rectangles are flush-adjacent, inside the available
      screen area, and form a single dock group.

## Acceptance / Definition of Done
- `koalaamp_se4_snap_tests` (offscreen) asserts every window lies inside
  `primaryScreen()->availableGeometry()`, that the EQ and playlist are flush against the player
  (`eq.topLeft() == (main.right() + 1, main.top())`, `playlist.topLeft() == (main.left(), main.bottom() + 1)`),
  and that all three are one dock group.
- Real X server (Xvfb): the three windows open in the classic arrangement (screenshot).
- The arrangement is remembered on the next launch (SE-4 persistence, unchanged).

## Out of scope
- A "snap the set into the classic arrangement" menu action — Winamp has none; docking is a drag
  gesture, and the initial arrangement is what makes it discoverable.
- Any change to the snap tolerance, the dock model, or the layout record format.
- Per-monitor placement policy beyond "centred on the primary screen, kept on it".

## References
- `docs/spec.md` §5.3 — the window set (snapping, docking, z-order)
- `docs/tickets/SE-4-window-manager.md` — the manager this builds on; `showAll()`, `recomputeDockGroups()`, `persistLayout()`
- `src/ui/WindowSet.{hpp,cpp}` — `rectOf()`, `dockGroup()`, `m_lastPos`

## Notes / risks
- Screen geometry is in Qt *logical* pixels, and the widget sizes (`size()`) already include the
  window scale — mixing device pixels in here would misplace the set on a HiDPI display.
- Keep the arrangement geometry-only (no visibility changes): `showAll()` / `restoreLayout()` stay
  the single source of truth for which windows are shown, and `showAll()` recomputes the dock groups
  after the positions are in place.
