# FE-14 spike — projectM in QOpenGLWidget

- **Status:** in review (2026-10-11) · **Owner:** FE-14 · **Depends:** FE-1 (hosting input), FE-0
- **Evidence:** `spikes/fe14_projectm/` (`spike_fe14`), JSON metrics to scratch paths
- **Supersedes:** the spec §6.1 assumption that projectM 4.1.x renders fine in `QOpenGLWidget`

This spike proves (or disproves) the flagship visualiser path: libprojectM
rendering MilkDrop presets inside a `QOpenGLWidget`, fed synthetic PCM, in a
masked and an unmasked frameless toplevel. Headless verification used Xvfb +
Mesa **llvmpipe** (software GL) — GPU numbers must come from CI/owner hardware.

## 1. Bottom line

1. **The QOpenGLWidget hosting works — but only on projectM ≥ 4.2 (`projectm_opengl_render_frame_fbo`).**
   The latest *release*, 4.1.8, cannot render into a QOpenGLWidget: its
   `projectm_opengl_render_frame()` hardcodes the draw framebuffer to **0**,
   while QOpenGLWidget renders into **its own FBO** (`defaultFramebufferObject()`).
   Result on 4.1.8: a permanently **empty (black) widget**. Reproduced with a
   minimal projectM-agnostic Qt program; 4.2.0-dev renders correctly. **The
   dependency pin moved to projectM master (`349b7cc2`, 4.2.0-dev) until 4.2.0
   ships.**
2. **Preset switches must run on the GL thread.** Loading a preset from a
   `QTimer` callback (no current GL context) produced `GL_INVALID_OPERATION`
   and, after a handful of switches, **permanent black output**. Queuing the
   load into `paintGL()` (the pattern projectM's own Qt frontend uses) fixed it.
3. **Masked vs unmasked (X11): no measurable difference.** A `QOpenGLWidget`
   child inside a `setMask()`-ed frameless toplevel renders correctly; a root
   screenshot proves XShape clipping with no compositor. 1080p software-GL FPS
   was statistically the same masked (59.6 avg) and unmasked (53.6 avg).
4. **Software-GL 1080p average is ~56 fps; `load_ms` is 1.9–20.9 ms**, so the
   300 ms switch-load budget is comfortably met and no GL context loss occurs.
   The "no black frame > 100 ms" half of that criterion is **not measurable
   with a centre-luma probe** — MilkDrop presets fade to black by design — see
   §5. (**GPU numbers pending.**)
5. Several presets render black/dark under this setup — see §6.

## 2. Acquisition (recorded)

The distro package is useless for this spike:

```
$ apt-cache policy libprojectm-dev
  Candidate: 2.1.0+dfsg-4ubuntu4     # < 4.0 — rejected
```

`scripts/vendor-deps.sh` was extended to **fetch + build a shared libprojectM**
into the gitignored `third_party/projectm/` (LGPL-2.1-or-later, dynamic link —
never committed, never statically linked):

| Component | Pin | SHA-256 |
|---|---|---|
| projectM | master `349b7cc2db312d47cef6be24c6856ced6643a385` (4.2.0-dev) | `6bf5d202…37ac` |
| projectm-eval (MIT submodule) | `22fb0cfd8f2dfbcd2b68f2443e7f44e19b32c09a` | `48002253…28ee` |

Built with `-DBUILD_SHARED_LIBS=ON -DENABLE_PLAYLIST=OFF -DENABLE_SYSTEM_PROJECTM_EVAL=OFF`;
installs headers + `lib/libprojectM-4.so` + a CMake package (`projectM4`). The
monolith is **not** statically linked. `--check` validates the stamp + `.so`;
`KOALAAMP_VENDOR_NATIVE=0` skips the native build.

**Why 4.2.0-dev instead of the pinned release 4.1.8:** see §3. This is a
deliberate, recorded deviation — the manifest (`deps/dependencies.json`,
`LICENSES.md`) carries the same note. Revisit when projectM 4.2.0 is released.

Test corpus: 10 varied presets from the community *cream-of-the-crop* pack,
fetched at a pinned commit by `spikes/fe14_projectm/fetch-presets.sh` into the
gitignored `third_party/presets/cream/` (third-party preset files are **never
committed**, per `AGENTS.md`). `spikes/fe14_projectm/run-spike.sh` runs the
spike headlessly under Xvfb with the required Qt env.

## 3. The 4.1.8 vs 4.2 framebuffer finding

`src/libprojectM/ProjectM.cpp` (4.1.8) ends a frame with:

```cpp
glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);   // hardcoded 0
```

A `QOpenGLWidget` has no useful framebuffer 0 — Qt renders into an FBO and
exposes it via `defaultFramebufferObject()`. Reading that FBO back after the
4.1.8 render gave a **mean luma of 0.0** (empty). projectM 4.2.0 master adds
`projectm_opengl_render_frame_fbo(instance, fbo)`, and reading
`defaultFramebufferObject()` after that call yields real content. projectM's
own `frontend-qt` (post-4.x port, 2026-02) uses exactly this call.

Consequence: **4.1.x cannot host the visualiser in a QOpenGLWidget at all.**
This overturns the ticket's `projectm_opengl_render_frame` expectation.

## 4. Rendering verification & hosting (masked vs unmasked)

Environment: Qt **6.10.2**, Xvfb + **llvmpipe** (LLVM 21.1.8, Mesa 26.0.8,
OpenGL 4.5 core), Qt platform `xcb` + `xcb_glx`.

Masked mode applies a 5-point **star** polygon via `QWidget::setMask` to a
frameless toplevel containing the `QOpenGLWidget` plus a sibling widget
(coexistence). With **no compositor**, the X root screenshot shows:

| Sample | masked | unmasked |
|---|---|---|
| corners (5,5 / 1275,5 / …) | `srgb(0,0,0)` (root) | rendered content |
| centre 40×40 mean | 163 | rendered content |

So XShape clipping is real and the GL child is clipped by the parent mask.
No black/misrendered frames were attributable to masking.

### FPS — 1080p, software GL, 10 presets, 5 s cycle, 60 s run

| Preset | masked fps | unmasked fps |
|---|---:|---:|
| 01 phat_RippleNebula9 | 62.7 | 63.6 |
| 02 Che-slowmotion | 77.6 | 62.4 |
| 03 376 | 50.2 | 61.3 |
| 04 milfography | 34.0 | 29.1 |
| 05 285 | 28.4 | 32.3 |
| 06 Telek-goodoldcollingwoodforever | 67.2 | 64.4 |
| 07 220 | 52.0 | 53.8 |
| 08 53 | 53.2 | 33.2 |
| 09 305 | 62.5 | 68.3 |
| 10 BrainStain-sunrays | 87.1 | 48.3 |
| **overall avg** | **59.6** | **53.6** |

_Software-GL baseline only: llvmpipe is CPU-bound and per-preset cost swings
~3×. Masked is not slower than unmasked. 720p llvmpipe averaged 93 fps
(hard-cut run) — resolution scaling is roughly linear, as expected._

## 5. Preset switch cost

`projectm_load_preset_file()` wall time (parse + compile of the new preset):
**1.9–7.4 ms** on the bright corpus across 60 s / ~30 switches (mean ≈ 3.5 ms),
and 1.9–20.9 ms over the full corpus (mean 4.0–8.4 ms) — far inside the
< 300 ms budget. Load failures are non-fatal: an invalid `.milk` kept the
current preset (projectM fell back to idle) and fired the `preset-failure`
callback; no crash.

**No context loss:** with preset loads deferred into `paintGL` (§1.2) the GL
error counter stays clean. Before the fix, loads from a timer produced
`GL_INVALID_OPERATION` and permanent black.

**On "no black frame > 100 ms":** a centre-luma black-frame detector reports
runs of 0.1–7 s during switching — but instrumenting the run start frames
shows most of them **begin mid-preset**, i.e. a MilkDrop preset fading to
near-black between its own scenes, *not* at the switch boundary. A naive
"> 100 ms black" gate therefore cannot be attributed to the switch, and this
measurement method cannot prove or disprove the criterion:

- bright corpus, 720p, 30 switches — smooth: `max_switch_black` 329 ms,
  load→first-non-black 368 ms; hard cut: 382 ms / 394 ms.
- But traced runs like `718 ms starting frame 855` began ~21 frames **before**
  the load at frame 876, with the *outgoing* preset active.

**Recommendation for VZ-1:** gate switch quality on `load_ms` + the
`preset_switch_failed` callback, not on screen luminance. If a black-gap gate is
wanted, it must sample immediately around the switch only and use
`projectm_set_preset_switch_requested_event_callback` to bracket it.

Command (720p switch sweep; the "bright" subset is the fetched corpus minus
the three dark presets 05/07/08):

```
spikes/fe14_projectm/run-spike.sh --width 1280 --height 720 \
    --duration 60 --cycle-ms 2000
```

## 6. Presets that misbehaved

projectM 4.2.0's logging callback (`projectm_set_log_callback`) was wired in
and works (an intentionally broken preset produced the expected
`[PerFrameContext] Could not compile per-frame code` error; the load was
rejected without a crash). Against the corpus projectM emitted **no shader or
compile errors** — only repeated texture-pack misses
(`WARN: [TextureManager] Failed to find requested texture "worms"`). The
offenders below load "successfully" but render nothing visible:

| Preset | Symptom | Evidence |
|---|---|---|
| `08_53.milk` | renders **black** (no content, no error) | mean luma 0.07–4.4 over all runs (incl. 10-min soak: 0.23) |
| `07_220.milk` | long dark stretches | mean luma 0.7–57; standalone black run 7.7 s |
| `05_285.milk` | very dark, intermittent black runs | mean luma 2.9–19.8; black runs up to ~2 s |

(Their high FPS in §4 is an artifact of drawing nothing.)

Caveat: the community **MilkDrop texture pack was not installed** — that is
what the `"worms"` warnings are. projectM falls back to procedural noise, so a
preset that samples a missing texture may render black/dim. These three are
therefore **candidates for a texture-pack follow-up, not confirmed projectM
bugs**.

## 7. Integration gotchas (for VZ-1)

1. **projectM ≥ 4.2 required** for `QOpenGLWidget` (§3).
2. **Dispatch preset loads (and any other GL-touching projectM call) from
   `paintGL`**, never from a timer/worker without the context current (§1.2).
3. **Qt module:** `Qt6::OpenGLWidgets` is required in addition to
   `Qt6::OpenGL`; added to the dependency manifest.
4. **Headless Qt:** under Xvfb the Qt Wayland plugin is preferred when
   `WAYLAND_DISPLAY` is set and its EGL path fails; force
   `QT_QPA_PLATFORM=xcb` + `QT_XCB_GL_INTEGRATION=xcb_glx`.
5. **Qt 6.10.2 teardown crash under Xvfb:** `QXcbWindow::needsSync()` segfaults
   during `QWidgetRepaintManager::flush` on exit (after all measurement). The
   spike exits via `std::_Exit(0)`; production must re-check this on real
   desktops (likely Xvfb-only). Not observed to affect rendering.

## 8. Soak (leak / crash)

600 s, masked toplevel, 720p, 10 presets, 5 s cycle (120 switches):

- **No crash, all 120 loads succeeded.** 51,998 frames, avg **86.7 fps**.
- **RSS is flat in steady state:** after the first 30 s it oscillates
  **302–319 MB** (range ≈ 16 MB) with no upward trend over the remaining 9.5
  min. Overall min/max was 268 → 320 MB, but the ≈ 52 MB span is **one-time
  warm-up** (projectM compiling all 10 presets + llvmpipe caches in the first
  half-minute). No evidence of a leak.
- projectM log: repeated
  `WARN: [TextureManager] Failed to find requested texture "worms"` — the
  missing MilkDrop texture pack (see §6).
- raw black metrics (`max_black_run` 5.4 s, `max_switch_black` 2.7 s) are
  confounded by the intrinsically dark presets 05/07/08 — see §5/§6.

Command:

```
spikes/fe14_projectm/run-spike.sh --mode masked --width 1280 --height 720 \
    --duration 600 --cycle-ms 5000
```

## 9. Cross-platform

Only Linux/X11 was exercised here. macOS (`QOpenGLWidget` under Cocoa with a
layer-masked frameless parent) and Windows (`SetWindowRgn` × GL swapchain) are
**unverified** pending FE-1's matrix / CI. No platform-specific code paths were
needed for Linux.

## 10. Decision

- **Hosting: confirm "visualiser in masked toplevel" for Linux/X11** — a GL
  child inside a `setMask`-ed frameless toplevel renders correctly with no
  measurable cost, no compositor required. Final Win/macOS confirmation is
  FE-1's; nothing here suggests overturning it, and the FBO path is
  platform-neutral.
- **projectM pin: move to ≥ 4.2** (currently `master @ 349b7cc2`). File/keep a
  reminder to switch to the 4.2.0 release tag when it ships.
- **VZ-1 must follow §7** — in particular, preset loads on the GL thread.
