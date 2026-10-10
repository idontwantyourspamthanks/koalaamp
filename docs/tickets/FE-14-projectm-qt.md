# FE-14 · Spike: projectM in QOpenGLWidget

- **Milestone:** M0 · **Est:** 2d · **Depends:** FE-1 (hosting decision), FE-0 · **Status:** in-review

## Story
Prove the flagship visualiser path: projectM 4.x rendering MilkDrop presets inside a Qt GL widget, fed PCM from (for now) generated noise. Follows projectM's own Qt-frontend pattern — the *supported* integration — and answers the two remaining unknowns: masked-window coexistence and preset-switch cost.

## Tasks
- [x] Vendor/build libprojectM (LGPL, dynamic) for the 3 platforms; pin the version. — _Pinned **4.2.0-dev (master @349b7cc2)** instead of 4.1.x: 4.1.x cannot host in a QOpenGLWidget (see Outcome §1). Fetched+built shared by `scripts/vendor-deps.sh`._
- [x] `VizCanvas : QOpenGLWidget` — create projectM in `initializeGL` (GL context current), push PCM in a timer loop, render in `paintGL`. — _Uses `projectm_opengl_render_frame_fbo` (4.2 API); preset loads are deferred into `paintGL` (required — see Outcome §2)._
- [x] PCM feed: synthetic noise + a 440Hz+440*1.5Hz mix so presets visibly react.
- [x] Load 10 presets from the cream-of-the-crop pack; cycle every 5s; measure switch latency. — _Corpus fetched at a pinned commit into gitignored `third_party/presets/cream` (`fetch-presets.sh`), never committed._
- [x] Test inside a masked toplevel AND an unmasked one (per FE-1's matrix — confirm or overturn its hosting decision with real rendering). — _Linux/X11 only; FE-1's report had not landed at time of writing._
- [x] Measure FPS at 1080p; note any preset that drops frames. — _Software-GL (llvmpipe) baseline recorded; iGPU/GPU numbers pending CI/owner._
- [ ] macOS: confirm QOpenGLWidget works under the Cocoa platform with a (layer-masked) frameless parent. — _Not testable on this Linux box; unverified._
- [x] Write `docs/spikes/projectm-qt.md`: hosting decision confirmed/overturned, FPS table, switch cost, presets that misbehave.

## Acceptance
- ≥ 55fps at 1080p on Win + Linux iGPU-class hardware (mac: renders + fps informational).
- Preset switch < 300ms with no context loss or black frame > 100ms.
- 10 presets cycle for 10 minutes without leak (RSS stable ±20MB) or crash.
- Hosting decision (masked vs unmasked toplevel) recorded with evidence.

## Outcome (2026-10-11) — full detail in `docs/spikes/projectm-qt.md`

Three findings dominate:

1. **projectM 4.1.x cannot host in a `QOpenGLWidget`.** Its
   `projectm_opengl_render_frame()` hardcodes draw framebuffer 0; QOpenGLWidget
   renders into its own FBO, so the widget stays black. **4.2.0's
   `projectm_opengl_render_frame_fbo()` is required**; the vendored pin is
   master `@349b7cc2` (4.2.0-dev) until 4.2.0 ships (`deps/dependencies.json`,
   `LICENSES.md` updated).
2. **Preset loads must run on the GL thread.** Loading from a timer produced
   `GL_INVALID_OPERATION` and permanent black after a few switches; deferring
   the load into `paintGL` fixed it.
3. **Masked vs unmasked (X11): no measurable difference** — a GL child inside a
   `setMask`-ed frameless toplevel renders correctly (root screenshot proves
   XShape clipping with no compositor). Decision below.

Acceptance status:

| Criterion | Result |
|---|---|
| ≥ 55 fps @ 1080p, iGPU | **Linux software-GL baseline ~56 fps avg** (masked 59.6 / unmasked 53.6); per-preset 28–87. iGPU/GPU numbers pending CI/owner. |
| Switch < 300 ms, no black > 100 ms | `load_ms` = **1.9–20.9 ms** ✔, no GL context loss ✔. The "black > 100 ms" half is **not measurable this way**: centre-luma black runs mostly begin *mid-preset* (MilkDrop fade-to-black), not at the switch — see report §5. Recommend gating on `load_ms` + `preset_switch_failed`, not luminance. |
| 10 presets × 10 min, RSS ±20 MB, no crash | **Pass** — 120 switches, no crash; steady-state RSS 302–319 MB (≈16 MB band), no trend; 52 MB min→max is warm-up only. |
| Hosting decision recorded | **Confirmed: viz in masked toplevel (Linux/X11).** |

Deviations: version pin 4.1.x → 4.2.0-dev; no Windows/macOS run (Linux box
only); FPS is software-GL, not iGPU.

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
