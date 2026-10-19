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
  on `src/ui/GenRenderer.{hpp,cpp}`: the gen frame assembled the way a classic gen window is
  (six 25 px title pieces — corners, transitions, a centred title plate, and the stretch tile
  that actually runs the full width — 11/8 px side borders, 125 px bottom corners with the
  resize grip, close box at the right end). The name is the sheet's own alphabet when it has
  one, centred, and `text.bmp` otherwise (the font's cell background is left transparent,
  so a sheet with no alphabet doesn't draw the title inside a box). The idle close button
  is the one drawn into the right title piece; the separate sprite only shows while the box
  is held. A skin with no `gen.bmp` wears its `pledit.bmp` chrome (VZ-2); the `pledit.txt`
  palette frame remains for a skin that has neither sheet. The window
  is at least 269 px wide so the two bottom corners fit. The canvas fills the
  client rect and the engine is injected, so the UI never links the
  back end. App wiring in `src/main.cpp`: the clutterbar V toggles the window, its close box hides
  it, `--always-on-top` covers it. Hosting: a gen frame is rectangular, so this window is unmasked —
  FE-14 §10 (masked toplevel) remains the answer for any skin that ever shapes one, and its
  macOS/Windows gap applies unchanged. It has since gained a slot in `WindowSet` — it snaps to its siblings
  and remembers where *and* whether it was open, while staying out of the dock groups, `showAll()` and the
  group drag (spec §5.3's "Vis Phase 1.5"; suite `koalaamp_se4_snap_tests`). Suite
  `koalaamp_vz1_viswindow_tests`._
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
  `paintGL`, so this is safe before GL exists). The shipped starter pack landed as four
  KoalaAmp-authored presets in `presets/`, compiled into the default roots ahead of the user folder
  (`KOALAAMP_BUNDLED_PRESETS`), so a fresh install opens on a real preset. Still open: the metadata
  cache the task-4 cycling UI wants. Suites `koalaamp_vz1_preset_tests`, `koalaamp_vz1_viswindow_tests`._

  On the starter pack: the community packs carry **no licence metadata at all** — a sample of 10
  cream-of-the-crop presets contains zero licence/copyright text, and the `.milk` format has no field for
  one (`docs/research/visuals.md` §6.4 already flags this as [UNVERIFIED]). "RL-1-cleared" therefore
  cannot be established by inspecting files; KoalaAmp-authored presets are clean by construction and are
  the cheap route.
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
- [x] Nightly preset soak: 500-preset corpus, 10s/preset timeout, crash/hang/FPS log. — _Built:
  `scripts/fetch-presets.sh` fetches a pinned, sorted slice (default 500; the FE-14 pin holds 9795)
  into the gitignored `third_party/presets/soak/`, and the app's `--soak-presets DIR --soak-hold-ms N`
  mode holds each preset in turn, logging frames and fps per preset and exiting 2 when one renders no
  frames during its hold (the ticket's hang; a crash is the process dying with the log showing the
  preset it died on). `PresetSoak` takes a frame-count callback rather than a window pointer, so it is
  unit-tested without a GL context (`koalaamp_vz1_preset_tests`). The nightly job is
  `.github/workflows/nightly-soak.yml` (schedule + manual, 500 x 10 s, log uploaded as an artifact).
  Verified end to end: first on the 10-preset cream corpus locally, then by the job itself — run
  39126467893 held 500/500 presets with zero stalls and exit 0 (99.6 min). The one heavy preset and the
  fps figures are in the Acceptance section below, along with what those figures do and do not mean._

## Acceptance
- 500-preset nightly: zero crashes/hangs. — **Met** (run 39126467893, 2026-10-18): 500/500 presets
  held, zero stalls, exit 0, 99.6 min wall. Per-preset fps under software GL at 1280x720: min 28.1,
  p5 57.3, median 61.8, p95 62.6 — but read that ceiling correctly: 62.6 is the canvas's own ~60 fps
  frame clock (a 16 ms tick), so median and p95 mean "kept up with the clock", not "the renderer's
  limit". The minimum is the only figure that shows a preset falling behind: `00224-EoS + Phat - chasers
  17 sentinels ghoul - nz+ ihavenothingtoffal likeness` at 28.1 fps over 281 frames (heavy, not hung;
  next lowest 53.4). `nightly-soak.yml` keeps running this nightly.
- ~~≥ 55fps p95 at 1080p on GTX1050-class~~ — **dropped (owner decision, 2026-10-18):** no GPU
  hardware available to the maintainer, and at this application's scale the visuals are not a
  performance concern. The nightly soak's software-GL figures above stand as an informational baseline
  (llvmpipe, 1280x720, the small gen-window canvas), and because the soak logs per-preset fps the number
  can be taken on real hardware later without new code.
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
