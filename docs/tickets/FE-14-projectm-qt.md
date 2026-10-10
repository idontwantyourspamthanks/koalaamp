# FE-14 · Spike: projectM in QOpenGLWidget

- **Milestone:** M0 · **Est:** 2d · **Depends:** FE-1 (hosting decision), FE-0 · **Status:** unstarted

## Story
Prove the flagship visualiser path: projectM 4.x rendering MilkDrop presets inside a Qt GL widget, fed PCM from (for now) generated noise. Follows projectM's own Qt-frontend pattern — the *supported* integration — and answers the two remaining unknowns: masked-window coexistence and preset-switch cost.

## Tasks
- [ ] Vendor/build libprojectM 4.1.x (LGPL, dynamic) for the 3 platforms; pin the version.
- [ ] `VizCanvas : QOpenGLWidget` — create projectM in `initializeGL` (GL context current), push PCM in a timer loop, `projectm_opengl_render_frame` in `paintGL`.
- [ ] PCM feed: synthetic noise + a 440Hz+440*1.5Hz mix so presets visibly react.
- [ ] Load 10 presets from the cream-of-the-crop pack; cycle every 5s; measure switch latency.
- [ ] Test inside a masked toplevel AND an unmasked one (per FE-1's matrix — confirm or overturn its hosting decision with real rendering).
- [ ] Measure FPS at 1080p on the weakest available GPU per platform (iGPU class); note any preset that drops frames.
- [ ] macOS: confirm QOpenGLWidget works under the Cocoa platform with a (layer-masked) frameless parent.
- [ ] Write `docs/spikes/projectm-qt.md`: hosting decision confirmed/overturned, FPS table, switch cost, presets that misbehave.

## Acceptance
- ≥ 55fps at 1080p on Win + Linux iGPU-class hardware (mac: renders + fps informational).
- Preset switch < 300ms with no context loss or black frame > 100ms.
- 10 presets cycle for 10 minutes without leak (RSS stable ±20MB) or crash.
- Hosting decision (masked vs unmasked toplevel) recorded with evidence.

## Out of scope
- The production VisualizerWindow, skin chrome, preset browser, user folders (VZ-1).
- Real audio input (FE-2's ring buffer gets wired in VZ-1).
- FBO/readback fallback path (only if masked-hosting fails — then this spike pivots to evaluate it).
- Preset corpus curation (RL-1's starter-pack audit).

## References
- `spec.md` §6.1 — host design
- `docs/research/visuals.md` §3.1, §6 — projectM API (`projectm_pcm_add_float`, `projectm_opengl_render_frame`), Qt frontends, known compat gaps (HLSL globals, SamplerState — rare)
- projectM's Qt frontend (repo: `projectM-visualizer/frontend-qt`) — the pattern to follow
- `docs/tickets/FE-1-masked-gl-window.md` — the hosting input

## Notes / risks
- If masked-window GL fails on Windows (SetWindowRgn × swapchain), do NOT burn the timebox fighting it: switch the spike to the unmasked-own-window hosting, confirm it, and record that as the decision — VZ-1 inherits it.
