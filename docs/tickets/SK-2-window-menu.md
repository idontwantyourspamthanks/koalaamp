# SK-2 · Main-window menu (skins, scaling, on top)

- **ID / title:** `SK-2 · Main-window menu`
- **Milestone:** M1 · **Est:** 1d focused · **Depends:** SE-2, SK-1, SE-4
- **Status:** in-progress

## Story
Winamp's main window opens a menu from its Option button, from the clutterbar's `O` and from a right-click anywhere on the window. KoalaAmp had the *seam* — `MainWindow::menuRequested` — but nothing was connected to it, so the only way to choose a skin was `--skin` on the command line, and the window scaling the renderers already support was unreachable from the UI.

## Tasks
- [x] Right-click on the main window emits `menuRequested` (Winamp's verb).
- [x] `WindowSet::setScale(int)` plus a virtual `SkinnedWindow::setScale`, so one choice scales every window *including* the type-erased visualizer slot.
- [x] Menu with `Scaling` (1x/2x/3x/4x, checkable), `Always on top` and `Quit`, rebuilt on open so the check marks are current.
- [x] `Skins` sub-menu: the bundled skins plus the skin database's recent list, deduplicated by
      name, the current skin checked, applying a choice without a restart. — _`skins::listSkins()`
      (sorted, `.wsz`/`.zip`, hidden skipped, a missing folder is just an empty list) feeds it, and
      one `applySkin` path now serves both startup and the menu (it also hands the skin to the
      visualizer, which the startup path had been skipping). Verified in the running app: the
      submenu lists the four bundled skins with the current one checked, and choosing another changes
      the window (compared over a region no menu covers)._
- [ ] Persist the chosen scale and on-top state (settings are PC-1's; per-session until then).
- [ ] Adopt the incoming skin's remembered layout on a switch, and persist the outgoing one first.
      Deliberately left out of `applySkin` for now: restoring there would race the startup path's
      `applyDefaultArrangement` fallback.

## Acceptance / Definition of Done
- Right-clicking the main window opens the menu.
- `2x` scales every window and the visualizer with it; `Always on top` lifts the whole set.
- `koalaamp_se4_snap_tests` covers the scale fan-out including the type-erased slot; `koalaamp_se2_interaction_tests` covers the right-click.

## Out of scope
- The full Options/settings dialog (PC-1 owns settings).
- A separate skin-browser window — this is a menu over the bundled folder and the recent list.
- Modern (`.wal`) skins.

## References
- `src/ui/MainWindow.hpp` — the `menuRequested` seam (SE-2)
- `src/ui/WindowSet.hpp` — the set's scale and keep-on-top seams
- `docs/spec.md` §5.3 — the window set
