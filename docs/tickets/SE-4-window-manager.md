# SE-4 · Window manager: snapping, docking, and the app window set

- **Milestone:** M1 · **Est:** 4d · **Depends:** SE-2, SE-3, SE-5 · **Status:** in-review

## Story
Winamp wasn't one window — it was a *set*: player, EQ, playlist snapped together, dragged as a group, shaded independently, remembered where you put them. This ticket delivers both halves: the window-set assembly in the app (including wiring the EQ window, which exists but is currently inert) and the snap/dock/z-order manager all three windows already left seams for.

## Part A — app window-set assembly (currently MISSING; this ticket owns it)
`src/main.cpp` today creates only MainWindow + PlaylistWindow; `EqWindow` is never instantiated, the main window's `eqToggled` signal has no handler, and `EqWindow::setEqSink`/`setAlbumEqStore` are never called — so **SE-5's window and AU-1's EQ are dead code in the running binary**. Fix in this ticket:
- [x] Instantiate `EqWindow`; skin it from the same `SkinSet` as the other two.
- [x] Wire the main window's EQ toggle (`MainWindow::eqToggled`) → show/hide, mirroring the playlist pattern (`main.cpp:117`).
- [x] Construct AU-1's chain and connect it through the sanctioned seam: `PlaybackCore::setPcmCallback(dsp.makeCallback(window.visFeed() ...))` — the DSP sits between the tap and the vis feed; check `src/audio/DspPipeline::makeCallback`'s downstream parameter for the exact chain order. EqWindow gets the `IEqSink` (its sliders then reach the real DSP) and the `AlbumEqStore`.
- [x] The three windows share the one `ITransport` (already the pattern for main+playlist).
- [x] App-level smoke: with a skin + the null backend, moving an EQ slider changes `nativeEqEnabled==false` DSP output (assert via the tap in a test), the EQ toggle shows/hides the window, and all three windows co-exist offscreen.

## Part B — snap/dock/z-order (the original SE-4 scope)
- [x] Snap manager service (app-level, not per-window): on drag end, snap within 10px (Winamp tolerance) to sibling edges/corners; pair offsets remembered; dock groups (adjacent windows move together on drag); un-drag on detach; keep-on-top option for the set (`config_aot`).
- [x] Consume the documented snap seams: `SkinnedWindow::dragFinished()` (SE-2/SE-3/SE-5 all emit it), the per-window layout state in `src/skins/SkinDb` (persist window positions/shades per skin).
- [x] Layout persistence: exact restore on launch (positions + shades + visibility for all three windows).
- [x] **Wayland**: request XWayland at launch (the Qt platform-name env route; documented fallback to compositor-placed windows), consistent with the FE-1 findings.
- [x] macOS: the app-level polygon hit-test already serves as the input mask (FE-1/SE-2 pattern) — nothing new; just don't regress it.
- [x] Scripted drag tests (offscreen + synthetic move events): snap/detach/group-move; layout save/restore round-trip.

## Acceptance
- **App smoke (Part A)**: EQ toggle shows/hides the window in the running app; slider → DSP gain within one block (tap-asserted, the SE-5 pattern at app level); all three windows render with a skin offscreen.
- Snap/detach/group-move scripted tests pass; layout restore is byte-exact (positions, shades, visibility).
- Window-set drag moves all docked windows with no flicker/tearing on the offscreen path (and note real-compositor verification as an owner item).
- Region.txt shapes still behave (Win/X11 real masks; macOS layer mask + hit-test) — regression check against the SE-2 window suite.
- ctest green (129/129 baseline + new); no edits under `src/audio`/`src/playback`/`src/interop` internals (the Part A wiring uses existing public seams only — grep-asserted).

## Out of scope
- Video/visualizer windows (VZ-1's). Modern-skin window system (Phase 3). Per-window double-size mode. Media library window (LI-3's).
- Any new DSP or engine behavior (Part A wires existing pieces; it does not add engine surface).

## References
- `docs/spec.md` §5.3 — the window-management design this implements
- `src/main.cpp` — the assembly to extend (the playlist toggle at :117 is the pattern; the gap is documented in Part A)
- `src/ui/EqWindow.hpp` (`setEqSink`/`setAlbumEqStore`), `src/ui/MainWindow.hpp` (`eqToggled`), `src/audio/DspPipeline.hpp` (`makeCallback`) — the Part A wiring points
- `src/ui/SkinnedWindow.*` — `dragFinished` seam + per-window state
- `docs/spikes/masked-gl-window.md` (FE-1) — mask/positioning behavior per platform incl. the Wayland findings
- `tests/se2_*`, `tests/se5_*` — the offscreen test patterns for window tests

## Notes / risks
- Part A is the load-bearing half: without it M1 "completes" with the EQ non-functional. Do it FIRST, as its own commit, so the wiring lands even if Part B needs more iterations.
- Snap math is simple; the risk is event-delivery differences on real compositors — offscreen tests prove the logic, owner verifies feel.
- `SkinDb` layout state was designed for exactly this (per-skin window layout) — use it, don't invent a second store.

## Evidence (in-review)

Branch `se4`; two commits: Part A `feat(ui): assemble the app window set and wire the EQ into AU-1's DSP (SE-4 A)`, Part B `feat(ui): snap/dock manager and per-skin window layout (SE-4 B)`.

- **ctest**: `134 → 142` tests, `100% tests passed, 0 tests failed` (`ctest --test-dir build/linux-gcc -j`, `QT_QPA_PLATFORM=offscreen`). Baseline before the ticket was green.
- **Part A app smoke** (`koalaamp_se4_window_set_tests`, offscreen): XWayland launch decision; `WindowSet` routes the EQ/PL toggles and the close buttons; an EQ slider reaches DSP output within one 1024-frame block through the production `DspEqSink`; the chain built as `dsp.makeCallback(feed.callback())` feeds the vis feed (order proved); with the libvlc core present, the same chain installed at `setPcmCallback` reports `nativeEqEnabled == false` (spec §3.1); all three windows render with `base-2.91.wsz`.
- **Part B** (`koalaamp_se4_snap_tests`, offscreen synthetic drags): snap to a sibling edge within 10 px and dock; a 25 px gap does not snap; the anchor drag carries the group (relative offsets preserved); dragging an attached window detaches it; keep-on-top flips all three flags; layout save/restore is byte-exact through `SkinDb` (positions, shades, playlist size, visibility); pre-SE-4 records restore visible.
- **App persistence**: ran the app twice against a temp `XDG_CONFIG_HOME`; the second run restores the edited layout exactly and re-saves it (`layout 10 20 285 20 10 136 ...`).

### Deviations / notes
- **Dock gesture**: the player window is the dock anchor — dragging it carries its docked group; dragging an attached EQ/playlist window detaches it (the rest stay put), re-docking when released flush. Recorded in `WindowSet`'s header. Offscreen proves the wiring; real-compositor feel remains an owner item.
- **`SkinLayoutState`** gained `mainVisible/eqVisible/playlistVisible` (flags bits 3–5, inverted = hidden); older records stay visible.
- **`config_aot`**: no settings store exists yet, so keep-on-top is exposed as `WindowSet::setKeepOnTop()` wired to the `--always-on-top` launch option (the future config plugin flips the same call).
- **Wayland**: `WindowPlatform::requestXWaylandAtLaunch()` forces `QT_QPA_PLATFORM=xcb` on a Wayland session before `QApplication`; an explicit platform (incl. offscreen) wins. Fallback documented in the header: XWayland unavailable → run with `QT_QPA_PLATFORM=wayland` (compositor-placed, alpha mask).
- No edits under `src/audio/`, `src/playback/` or `src/interop/` internals (Part A adds a constructor to the `src/ui` `PlaybackCoreTransport`); the SE-3 grep guards stay green.
- No WCL/GPL source was consulted for this change.
