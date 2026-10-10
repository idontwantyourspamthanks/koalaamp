# VZ-1 · projectM visualizer window

- **Milestone:** M2 · **Est:** 5d · **Depends:** FE-14, FE-4 · **Status:** unstarted

## Story
The production visualizer: a skin-chromed window where projectM renders the user's MilkDrop preset folder, driven by real post-DSP PCM from the playback pipeline — random/ordered cycling, hot-switching, fullscreen, and the curated starter pack shipped in the installer.

## Tasks
- [ ] `VisualizerWindow` with the hosting mode FE-14 recorded (masked vs unmasked toplevel); skin chrome from `gen`/visspec-class bitmaps with designed fallback.
- [ ] PCM wiring: PcmTap ring buffer → `projectm_pcm_add_float` (interleaved L/R) throttled to ~30Hz or per-frame; same post-DSP feed as the main-window spectrum.
- [ ] Preset store: user folders watched (QFileSystemWatcher) + recursive scan for `.milk`/`.milk2`; the shipped starter pack (RL-1-cleared subset); preset metadata cache for instant cycling.
- [ ] Cycling: random / ordered / locked; next/prev hotkeys + on-screen skinned control; preset name overlay on switch (Winamp-style, auto-fade).
- [ ] Hot-swap: `projectm_load_preset_file` without window/context teardown (FE-14's measured switch cost as the budget).
- [ ] Fullscreen mode (own toplevel, exclusive-mode borderless); ESC/exit restores.
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
