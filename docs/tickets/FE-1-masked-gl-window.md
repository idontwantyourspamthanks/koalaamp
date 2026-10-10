# FE-1 · Spike: masked frameless window + QOpenGLWidget

- **Milestone:** M0 · **Est:** 3d · **Depends:** FE-0 · **Status:** unstarted

## Story
Retire the biggest Qt-specific rendering unknown before M1 commits to the window architecture: **does `QWidget::setMask` coexist with a GL swapchain child in one frameless toplevel on all three platforms?** The answer picks where the visualiser lives (masked toplevel vs separate unmasked window) and proves the region.txt mechanism end-to-end.

## Tasks
- [ ] Minimal Qt app: frameless toplevel (`Qt::FramelessWindowHint`), ~275×116, paint a plain color.
- [ ] Apply a `region.txt`-style polygon via `setMask(QRegion)` — use a deliberately non-rectangular polygon (star / L-shape), not a rectangle, so clipping is unambiguous.
- [ ] Add a child `QOpenGLWidget` painting a spinning triangle; ensure `QSurfaceFormat` compatible with the raster surface.
- [ ] Test matrix:
  - [ ] Windows 10/11 (SetWindowRgn path — real region, input clipped by OS)
  - [ ] Linux X11 (XShape path)
  - [ ] Linux Wayland session via XWayland (mask + self-positioning both work?) and native Wayland (expected: mask inert → document the degradation: alpha window + compositor placement)
  - [ ] macOS (CALayer mask: visual clipping only — clicks outside must be rejected by our app-level point-in-polygon hit-test)
- [ ] For each combo record: pixels clipped? input clipped (OS or app hit-test)? GL renders inside mask? resize behavior?
- [ ] Write `docs/spikes/masked-gl-window.md` with the matrix + the hosting decision for FE-14/VZ-1.

## Acceptance
- Polygon clipping visibly works on Win + X11 with **no compositor running** (start a bare X session or disable the WM compositor to prove it).
- GL triangle animates ≥ 55fps inside the masked window on Win + Linux (mac: renders, fps informational).
- macOS: clicks outside the polygon are rejected by the app hit-test (log line proves it).
- Spike report exists with the per-OS matrix and a one-line decision: "viz in masked toplevel" or "viz in own unmasked toplevel, per-OS".

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
