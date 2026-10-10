# FE-1 spike — masked frameless toplevel + QOpenGLWidget child

- **Status:** decided (2026-10-10) · **Owner:** FE-1 · **Ticket:** `docs/tickets/FE-1-masked-gl-window.md`
- **Probe:** `spikes/fe1/spike_fe1.cpp` · **Driver:** `spikes/fe1/x11_matrix.sh`
- **Evidence:** `docs/spikes/fe1-evidence/` · **Spec:** §5.2-5.3, §6.1

## Verdict (the one-line decision)

**Viz in the masked toplevel.** A `QOpenGLWidget` child renders into an FBO that
is composited into the *raster* toplevel, so there is no GL swapchain on the
masked window itself — `setMask` and GL coexist on every path measured
(X11 with and without a compositor, XWayland). Keep "viz in its own unmasked
toplevel" only as the contingency if the Windows owner check
(`SetWindowRgn` + D3D flip) turns up flicker (spec §6.1's stated mitigation).

## Question

Does `QWidget::setMask` coexist with a GL swapchain child in one frameless
toplevel, does the mask clip **pixels and input**, and does it hold up with **no
compositor running** — the property that makes masks beat alpha (spec §5.3)?

## Method

`spike_fe1` is a standalone probe (does not link `koalaamp_ui`): a frameless
275×116 toplevel, a non-rectangular mask from `region.txt`-style polygons
(`star` default, `lshape`), a child `QOpenGLWidget` spinning a triangle
(core-profile 3.3 shaders), a magenta window background and a distinct GL
clear colour so clipping is unambiguous. It logs `SPIKE|…` lines (platform,
geometry, inside/outside probe points, every mouse press with a ray-cast
point-in-polygon verdict, fps). Options: `--polygon`, `--duration`, `--move`,
`--frame-ms`, `--resize WxH`, `--inject-click`.

`x11_matrix.sh <DISPLAY> <label> <outdir>` drives the X11 cells: it launches the
probe, reads the window geometry from the **X server** (`xwininfo`, not the
app), captures `xwininfo -shape`, grabs the window with `ffmpeg x11grab`,
probes inside/outside pixels, then injects **real** `xdotool` clicks at a point
outside the polygon (must reach no-one) and one inside (must be logged).
Physical coords are derived from the X-window size so the test also works under
a fractional-scaling compositor.

## Matrix

Legend: **YES/NO** = measured; *owner* = not testable on this Linux box;
`[INF]` = inference, not observed.

| Platform (this machine) | Region API | Pixels clipped | Input clipped | GL inside mask | Self-positioning | Resize | fps |
|---|---|---|---|---|---|---|---|
| **Linux X11, no compositor** (Xvfb `:95`, X.Org, no WM) | XShape (`XShapeBounding`) | **YES** | **YES** (OS) | **YES** | n/a (X11 move works) | mask does **not** scale | 4023 (vsync off, llvmpipe) |
| **Linux X11 via XWayland + KWin** (`:0`, composited) | XShape via Xwayland | shape applied & DPR-scaled `[INF]` for pixels | XShape semantics; not injectable | **YES** (renders) | **YES** (requested 100,100 honoured) | — | 59.7 (vsync, 60 Hz) |
| **Linux native Wayland** (kwin_wayland) | none (no platform mask) | **NO** — mask inert `[INF]` visual; Qt clips its paint region only | **NO** (OS) → app hit-test required | **YES** (renders) | **NO** (compositor places) | — | 59.9 |
| **Windows 10/11** | `SetWindowRgn` | *owner* | *owner* | *owner* | *owner* | *owner* | *owner* |
| **macOS** | CALayer shape (visual only) | *owner* | **app hit-test** (code present) | *owner* | *owner* | *owner* | *owner* |

### Linux evidence (measured)

**X11, no compositor — `docs/spikes/fe1-evidence/xvfb-nocompositor.*`** — the
headline acceptance cell:

```
Window shape extents:  105x98+85+4        # < 275x116 → non-rectangular star is live
pixel inside=srgb(255,166,26)             # GL triangle at star centre
pixel outside=srgb(0,0,0)                 # window corner (3,3) clipped → root, not magenta
press local=137,58 hit=ACCEPT             # click at inside point delivered
(no press line)                           # click at outside point (103,103) never reached the app
fps frames=61106 elapsed_ms=15190 avg=4022.8
```

The outside click producing **no** log line while the inside click does is the
OS-level input clipping proof; the outside pixel being black (root) rather than
magenta is the pixel-clipping proof; and there is no compositor on `:95` at all.
`xvfb-nocompositor.screen.png` shows the star with the orange triangle clipped
by the arms. The `L`-shape spot check (`xvfb-lshape.*`) repeats all three:
inside `srgb(255,0,255)`, outside `srgb(0,0,0)`, outside click unlogged.

**Resize** (`xvfb-resize.*`): resized the toplevel 275×116 → 400×220; the X
shape stays `105x98+85+4` — the mask is in *widget-local logical* coords and
does **not** scale with the window, so a resizable masked window must re-apply
its region on resize.

**XWayland + KWin — `xwayland-kwin.*`**:

```
Window shape extents:  152x142+123+6      # 105x98+85+4 scaled by dpr 1.44792 → mask applied
Absolute upper-left:   145,145            # requested logical 100,100 × 1.44792 → move() honoured
fps frames=891 elapsed_ms=14917 avg=59.7
```

So **mask and self-positioning both work under XWayland**; the mask is
correctly scaled for the compositor's fractional DPR. Pixel/input injection
could not be repeated on `:0` (Xwayland's `XWarpPointer` does not drive the
Wayland compositor pointer, and a root `x11grab` under a compositor returns
black), so those two sub-cells are covered by the bare-X11 run above — the
underlying mechanism is the same `XShapeBounding` region.

**Native Wayland — `wayland-native.log`**: `platform=wayland`; `--move 100,100`
is ignored (`geom x=0 y=0`) because Wayland has no client-selectable window
position — the compositor places the window. Qt's Wayland platform window
implements no mask, so the mask is inert at the surface/compositor level (the
`wayland-native-backingstore-grep.txt` log shows Qt still clips its *widget
paint flush* to the mask region — a Qt-side effect, not a surface shape or
input region). Input therefore is `[INF]` not clipped by the OS, which is why
the app-level hit-test exists: a synthetic press at local `(3,3)` (outside the
star) is logged `hit=REJECT`, and `(137,58)` `hit=ACCEPT`. This is the
documented degradation (`spec` §5.3): native Wayland degrades to an
alpha/compositor-placed window; the project's XWayland request stands.

## Windows — needs owner verification

Windows cannot be tested from this Linux box. Build and run `spike_fe1`
(`cmake --preset windows-msvc`), then capture the following. The risk is
specific and pre-identified: `SetWindowRgn` + a D3D flip-model swapchain can
flicker (ticket Notes). Because `QOpenGLWidget` composites through an FBO into
the raster toplevel, the masked window has no swapchain of its own — the
expectation is that this risk does **not** reproduce, but confirm it.

1. `spike_fe1 --polygon star --duration 15 --move 200,200`; click a corner
   pixel of the 275×116 rect (outside the star) and the centre (inside). The
   log must show **no** press for the outside click and `hit=ACCEPT` for the
   inside one — i.e. the OS clipped input via `SetWindowRgn`.
2. Confirm the GL triangle animates and check the `fps avg=` line ≥ 55; watch
   for flicker/tearing inside the mask over the 15 s.
3. `--resize 400x220`: does the region stay fixed (Linux behaviour) or follow
   the window?
4. Capture: a screenshot of the star, the console log, and the fps line.

## macOS — needs owner verification

The mask is a CALayer *visual* clip only; clicks outside the polygon **do**
reach the app, so the app-level point-in-polygon hit-test is mandatory. The
probe already implements it.

1. `spike_fe1 --polygon star` and click a window corner outside the star: the
   log must show `press … hit=REJECT` (this log line is the acceptance proof).
   Click the centre: `hit=ACCEPT`.
2. `spike_fe1 --polygon star --inject-click` gives the same result
   deterministically, without a mouse.
3. Confirm the triangle renders and record `fps avg=` (informational only).
4. Capture: the console log showing the REJECT/ACCEPT pair, and a screenshot.

## Decision and follow-ups

- **FE-14 / VZ-1: host the viz in a masked toplevel** (QOpenGLWidget child).
  The "own unmasked toplevel" fallback stays available if the Windows check
  fails; on macOS the app hit-test (already in the probe) is required
  regardless.
- **Wayland:** masks are inert; the XWayland-request path from spec §5.3 is the
  supported route, with the alpha/compositor-placed window as the degraded
  fallback. The app hit-test must exist on every platform.
- **Resize:** any resizable masked window (playlist editor) must re-apply its
  `QRegion` on resize — the mask does not scale with the widget.
- FE-14 should add `Qt6::OpenGLWidgets` to the root `find_package` and to
  `deps/dependencies.json`'s Qt `modules` list when it lands in app code
  (FE-1 declares it only in `spikes/fe1/CMakeLists.txt`).

## Deviations / limitations

- The "X11 with a discrete compositor" cell is covered by XWayland + KWin
  (composited X11); this box has no non-XWayland X server with a compositor,
  and pinning one was judged out of scope. The required **no-compositor** cell
  is the bare `Xvfb` run.
- Native-Wayland pixel/visual and OS-input cells could not be measured
  programmatically (no screenshot portal, no working absolute pointer
  injection); they are marked `[INF]`/owner-eyeball above.
- `[INF]` rows are the only unobserved claims; everything else is quoted from
  `docs/spikes/fe1-evidence/`.
