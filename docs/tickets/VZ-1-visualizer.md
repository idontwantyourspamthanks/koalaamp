# VZ-1 · projectM visualizer window

- **Milestone:** M2 · **Est:** 5d · **Depends:** FE-14, FE-4 · **Status:** in-progress

## Story
The production visualizer: a skin-chromed window where projectM renders the user's MilkDrop preset folder, driven by real post-DSP PCM from the playback pipeline — random/ordered cycling, hot-switching, fullscreen, and the curated starter pack shipped in the installer.

## Tasks
- [x] Engine seam + canvas host: `IVisualizerEngine` with the GL-context-bound lifecycle
  FE-14 measured (create/destroy/load require a current context; `render` draws into the
  widget's FBO), `createEngine()` plus a stub whose `available()` is false in a build
  without libprojectM, and `VisualizerCanvas` — a `QOpenGLWidget` that drains a
  `StereoPcmRing` on its own ~60fps frame clock and applies queued preset loads inside
  `paintGL`. — _`src/interop/projectm/`, `src/ui/VisualizerCanvas.*`; suites
  `koalaamp_vz1_engine_tests` and `koalaamp_vz1_window_tests`._
- [x] `VisualizerWindow` with the hosting mode FE-14 recorded (masked vs unmasked toplevel); skin
  chrome from `gen`/visspec-class bitmaps with designed fallback. — _`src/ui/VisualizerWindow.{hpp,cpp}`
  on `src/ui/GenRenderer.{hpp,cpp}`: the tiled gen frame (25 px title/bottom tiles, 29 px side
  borders, close box, title in the skin font) and a designed fallback for the many skins that omit
  `gen.bmp`. The canvas fills the client rect and the engine is injected, so the UI never links the
  back end. App wiring in `src/main.cpp`: the clutterbar V toggles the window, its close box hides
  it, `--always-on-top` covers it. Hosting: a gen frame is rectangular, so this window is unmasked —
  FE-14 §10 (masked toplevel) remains the answer for any skin that ever shapes one, and its
  macOS/Windows gap applies unchanged. It has since gained a slot in `WindowSet` — it snaps to its siblings
  and remembers where *and* whether it was open, while staying out of the dock groups, `showAll()` and the
  group drag (spec §5.3's "Vis Phase 1.5"; suite `koalaamp_se4_snap_tests`). Suite
  `koalaamp_vz1_viswindow_tests` (5 cases)._
- [x] PCM wiring: PcmTap ring buffer → `projectm_pcm_add_float` (interleaved L/R) throttled to ~30Hz or
  per-frame; same post-DSP feed as the main-window spectrum. — _There is no `PcmTap` type: the seam is
  `PcmFanout` (`src/ui/PcmFanout.hpp`), and the app now installs
  `dsp.makeCallback(pcmFanout.callback())` with the spectrum's VisFeed and the canvas as consumers
  (`VisualizerCanvas::pcmCallback()`, `src/main.cpp`) — the visualizer sees the same post-DSP samples,
  and a second reader can never replace the DSP chain. The canvas drains its ring per frame in
  `paintGL`; `ProjectMEngine::pushPcm` chunks without allocating and calls `projectm_pcm_add_float`._
- [ ] Preset store: user folders watched (QFileSystemWatcher) + recursive scan for `.milk`/`.milk2`;
  the shipped starter pack (RL-1-cleared subset); preset metadata cache for instant cycling. —
  _`PresetStore` (`src/ui/PresetStore.{hpp,cpp}`) landed: the recursive scan (hidden entries skipped,
  like the library scanner), a debounced `QFileSystemWatcher` over the roots *and* every directory the
  scan walked — a root that does not exist yet is watched through its nearest existing ancestor, so a
  folder dropped in later is picked up — and ordered/random selection with wrapping. The app takes
  `--presets DIR`, loads the first preset, and follows the selection (the canvas queues loads into
  `paintGL`, so this is safe before GL exists). Still open: the shipped starter pack (RL-1's) and the
  metadata cache the task-4 cycling UI wants. Suite `koalaamp_vz1_preset_tests`._
- [ ] Cycling: random / ordered / locked; next/prev hotkeys + on-screen skinned control; preset name
  overlay on switch (Winamp-style, auto-fade). — _`PresetCycler` (`src/ui/PresetCycler.{hpp,cpp}`):
  ordered/random (the order is delegated to the store, which never repeats the current preset in
  random), locked = auto-advance off with manual N/P still working, and a settable interval (default
  30 s; `--preset-cycle-ms`, 0 = locked, until settings own it). The window's MilkDrop keys (N/P/R/L)
  emit requests the app wires to the cycler, and every switch is announced by `VisualizerOverlay`
  (`src/ui/VisualizerOverlay.{hpp,cpp}`): the skin's 5x6 font on a translucent plate over the render,
  fading out after 2 s and starting its clock when actually mapped. The overlay is a raised *sibling*
  of the GL canvas so projectM's GL state is never interleaved with Qt painting. Still open: the
  on-screen skinned control — classic skins carry no visualizer buttons, so it needs `genex.bmp`
  widget art, which LI-3 owns. Suites `koalaamp_vz1_preset_tests`, `koalaamp_vz1_viswindow_tests`._
- [ ] Hot-swap: `projectm_load_preset_file` without window/context teardown (FE-14's measured switch cost as
  the budget). — _No teardown happens by construction: the canvas queues loads into `paintGL` and calls
  `loadPresetFile` on the live engine, and neither the canvas nor `ProjectMEngine` ever re-creates the
  projectM instance or the GL context for a load. Evidence: FE-14's measured 1.9-7.4 ms per switch
  (`docs/spikes/projectm-qt.md` §5) and the cycling runs from this work, where consecutive screenshots
  show different presets rendered from the same window and context. Still open: the acceptance's in-app
  p95 at 1080p on real GPU hardware._
- [ ] Fullscreen mode (own toplevel, exclusive-mode borderless); ESC/exit restores. —
  _`VisualizerWindow::setFullscreen` (F toggles, ESC leaves): borderless fullscreen with the gen chrome
  dropped and the canvas taking the whole window; leaving restores the windowed size, shape and geometry.
  Offscreen-tested (state, the chrome-less layout, ESC restoring, and a resize requested while fullscreen
  still applying afterwards). The real-surface check under Xvfb proves only the chrome-less takeover —
  there is no window manager there to honour `showFullScreen`, so the geometry stays windowed. Win/macOS
  and a real desktop remain unverified, and "the window remembers state" needs the settings store._
- [ ] Per-preset `sampler_*` textures resolved from the preset's directory (projectM 4.2 sprite API); base texture pack shipped.
- [ ] Nightly preset soak: 500-preset corpus, 10s/preset timeout, crash/hang/FPS log.

## Acceptance
- 500-preset nightly: zero crashes/hangs; ≥ 55fps p95 at 1080p on GTX1050-class.
- Preset switch < 300ms, no context loss, name overlay shows.
- User adds a folder of presets → appears in the browser without restart (watcher).
- Fullscreen enters/exits cleanly on all 3 OSes; window remembers state.
- Audio-reactivity parity: known beat-heavy track drives preset motion (manual checklist item with the FE-14 test signal).

## Out of scope
- AVS (Phase 3). MilkDrop3 `.milk2` double-blend (projectM handles single presets).
- Preset editing, recording, screenshot export.
- Beat/BPM detection in the host (projectM does its own).

## References
- `spec.md` §6.1-6.2 — host design + classic-vis sharing
- `docs/research/visuals.md` §3.1, §5-6 — projectM API surface, PCM feed contract, texture/sprite notes, macOS GL caveat
- `docs/tickets/FE-14-projectm-qt.md` — the spike this productionizes
- projectM preset packs (cream-of-the-crop; MegaPack stays user-supplied — RL-1 audits the starter subset)

## Notes / risks
- PcmTap backpressure: the visualizer must never block the audio callback — drop frames silently if the ring is full (audio correctness > visual completeness).
- macOS GL deprecation is a monitored long-term risk (feasibility §5): keep the IVisualizerEngine seam so a Metal renderer can slot in later without touching the window code.
