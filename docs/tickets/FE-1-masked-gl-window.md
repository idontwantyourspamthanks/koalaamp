# FE-1 · Spike: masked frameless window + QOpenGLWidget

- **Milestone:** M0 · **Est:** 3d · **Depends:** FE-0 · **Status:** in-review

> Spike report: [`docs/spikes/masked-gl-window.md`](../spikes/masked-gl-window.md) (matrix, evidence, hosting decision). Linux cells verified empirically; Windows/macOS cells need owner verification (test code + instructions in the report).

## Story
Retire the biggest Qt-specific rendering unknown before M1 commits to the window architecture: **does `QWidget::setMask` coexist with a GL swapchain child in one frameless toplevel on all three platforms?** The answer picks where the visualiser lives (masked toplevel vs separate unmasked window) and proves the region.txt mechanism end-to-end.

## Tasks
- [x] Minimal Qt app: frameless toplevel (`Qt::FramelessWindowHint`), ~275×116, paint a plain color. (`spikes/fe1/spike_fe1.cpp`)
- [x] Apply a `region.txt`-style polygon via `setMask(QRegion)` — star and L-shape both implemented.
- [x] Add a child `QOpenGLWidget` painting a spinning triangle; `QSurfaceFormat` (core 3.3) set before `QApplication`.
- [~] Test matrix:
  - [ ] Windows 10/11 (SetWindowRgn path) — **needs owner verification**; run instructions + evidence list in the report.
  - [x] Linux X11 (XShape path) — bare X server, no compositor: pixels + input clipped, GL renders, fps.
  - [x] Linux XWayland (KWin): mask applied (DPR-scaled) and self-positioning honoured; native Wayland: mask inert, self-positioning ignored, app hit-test required.
  - [ ] macOS — **needs owner verification**; app hit-test implemented in the probe; run instructions in the report.
- [x] For each combo record: pixels clipped? input clipped (OS or app hit-test)? GL renders inside mask? resize behavior? (matrix in the report + raw evidence in `docs/spikes/fe1-evidence/`)
- [x] Write `docs/spikes/masked-gl-window.md` with the matrix + the hosting decision for FE-14/VZ-1.

## Acceptance
- [x] Polygon clipping visibly works on **X11 with no compositor running** (bare `Xvfb`; `xwininfo -shape` + pixel probes + real `xdotool` clicks). Windows half **needs owner verification**.
- [x] GL triangle animates ≥ 55fps inside the masked window on Linux (4023 X11-no-compositor, 59.7 XWayland, 59.9 Wayland). Windows **needs owner verification** (mac: informational).
- [ ] macOS: clicks outside the polygon are rejected by the app hit-test (log line proves it) — **needs owner verification**; code path implemented (`--inject-click` gives it deterministically).
- [x] Spike report exists with the per-OS matrix and a one-line decision: **"viz in masked toplevel"** (own-unmasked fallback only if the Windows check fails).

## Out of scope
- Skin rendering, sprites, actual WSZ parsing (FE-3).
- projectM itself (FE-14).
- Snapping/docking (SE-4).
- Performance tuning beyond the fps sanity check.

## References
- `spec.md` §5.2-5.3 — mask + window-management design
- `docs/research/frameworks.md` §0, §3 — masks vs alpha, Wayland wall, Qt setMask evidence
- `docs/research/platform.md` §1.5-1.6 — (Avalonia-era transparency matrix; superseded for stack choice, useful for the compositor test setup)
- Qt source verified 2026-10-10: `qwindowswindow.cpp:3152` (SetWindowRgn), `qcocoawindow.mm:1324` (CAShapeLayer) — see feasibility §1a

## Notes / risks
- Windows: `SetWindowRgn` + D3D flip-model swapchains have a documented interaction history; if GL flickers inside the mask, that *is* the finding — fall back to "viz in own toplevel" and don't burn more time.
- Don't skip the no-compositor X11 test — it's the whole reason masks beat alpha.
