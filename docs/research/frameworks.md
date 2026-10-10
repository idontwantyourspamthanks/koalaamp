# UI Framework Alternatives (post-Avalonia review)

Scope: research triggered by the question "Avalonia is no good then — need the transparency; the whole point is faithful recreation of the weird and wonderful skins. Any good alternatives?" Date 2026-10-10. Cited claims carry URLs; [UNVERIFIED]/[INFERENCE] marked.

## 0. The mechanism primer — why "transparency" is the wrong word

Classic Winamp's weird shapes come from **window masks, not transparency**. A `.wsz`'s optional `region.txt` lists polygons that Winamp fed to Win32 `SetWindowRgn`; the X11 equivalent is the Shape extension (XShape). Masks are *geometry*: the OS clips both drawing **and input** outside the polygon, and they work **with no compositor** ([MS SetWindowRgn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowrgn); [XShape via Avalonia discussion](https://github.com/AvaloniaUI/Avalonia/discussions/13827)).

Per-pixel **alpha** is a different mechanism (compositing). Every alpha path — Electron, Tauri, Avalonia, Flutter, Godot, JavaFX — needs a compositor on Linux and cannot reproduce a hard polygon edge or input clipping natively.

**The Wayland wall:** xdg-shell has `wl_surface.set_input_region` (click-through only) and **no output-shaping request at all**. Chromium's Wayland backend confirms: `UpdateWindowMask()` sets only the input region, with a live TODO for real `SetShape` support ([wayland_toplevel_window.cc](https://chromium.googlesource.com/chromium/src/+/c207f795b977631c1ab8a406159a3164233fdee5/ui/ozone/platform/wayland/host/wayland_toplevel_window.cc), crbug.com/1158733). So on **native Wayland no framework can mask a window** — masks work on X11 and XWayland only. Consequence for us: classic skins render true on Win/macOS/X11; on native Wayland *everyone* is capped at alpha (with click-through via input region). XWayland fallback covers the rest.

## 1. Per-candidate matrix

| | (a) alpha per-OS | (b) masks | (c) click-through | (d) multi-window | (e) visualizer | (g) audio/sidecar | license | verdict |
|---|---|---|---|---|---|---|---|---|
| **Electron 44 + Webamp + Butterchurn** | Win/mac OK (Win GPU accel transparency bug [#40515](https://github.com/electron/electron/issues/40515)); Linux needs compositor | None — fake with alpha+hit-test | Whole-window `setIgnoreMouseEvents({forward})` + JS hit-test | Multi BrowserWindow supported, but Webamp ships as **one overlay window** ([#494](https://github.com/captbaritone/webamp/issues/494)) | **Butterchurn WebGL2 in-page, zero native** | Webamp decodes via Web Audio; libvlc as sidecar if needed (PCM→AudioWorklet) | MIT stack | **Speed-to-fidelity winner** |
| **Tauri 2.12** | Linux broken-ish: WebKitGTK/NVIDIA transparency failures ([wry#1667](https://github.com/tauri-apps/wry/issues/1667)) | None | No first-class API | First-class | WebGL in webview (Butterchurn OK) | Sidecar feature is a strength | MIT/Apache | WebKitGTK is the persistent liability |
| **Qt 6.12 LTS** | Win/mac OK; compositor-dep on X11 | **`QWidget::setMask` — the only real mask API in a mainstream framework** ([docs](https://doc.qt.io/qt-6/qwidget.html#setMask)); maps to SetWindowRgn/XShape; **broken on native Wayland** ([forum](https://forum.qt.io/topic/154266/windowtransparentforinput-not-worked-on-wayland)) | Best-in-class on X11 | Native top-levels; Wayland clients cannot self-position (Qmmp can't be dragged on GNOME Wayland — [RH bug](https://bugzilla.redhat.com/show_bug.cgi?id=1929763)) | **projectM is itself a Qt project** (Qt5/Qt6 frontend) | In-process libvlc / QtMultimedia (FFmpeg-backed, qtamp uses it) | LGPLv3 (dynamic link OK for closed) | **Fidelity ceiling + long-term quality winner** |
| **Flutter** | No core API; per-OS plugin hacks | None | None | **Multi-window still experimental** ([Aug 2026 blog](https://flutter.dev/blog/desktop-windowing-apis)) | No WebGL; external-GL rough | FFI possible | BSD-3 | **Not viable as the shell** — user's doubt confirmed |
| **JavaFX 25** | `StageStyle.TRANSPARENT` is a conditional feature | None | Not exposed | Yes | projectM/GL interop painful | vlcj mature (5.0.0-M4, targets LibVLC 4) | GPLv2+CE | Niche |
| **Godot 4.5** | Per-pixel setting, compositor-dep Linux | None | In-app only; OS-level needs native code | Sub-windows | Strong GL; projectM as GDExtension = bespoke | GDExtension wrap | MIT | Non-game-app ecosystem immature |
| **Avalonia 12.1 + shims** | Win/mac OK; compositor-dep X11 | **No API** — P/Invoke SetWindowRgn/XShape yourself | No built-in; "not possible at all on Wayland" ([#13827](https://github.com/AvaloniaUI/Avalonia/discussions/13827)) | Supported | Own GL interop + own projectM binding | LibVLCSharp mature | MIT | **[UNVERIFIED] whether an OS region clips Avalonia's Skia/GPU swapchain at all** — spike-only |

## 2. Reference implementations verified

- **Webamp** — MIT, 11.3k★, **last release v2.3.1 (Jun 2026)**, active commits; parses `region.txt` (guards malformed files), `.ani` cursors, pixel-exact sprites; the Skin Museum (~100k skins) is its de-facto corpus oracle. Butterchurn (MIT, 2.6.7) integrated via `webamp/butterchurn`. Commercial desktop use permitted by MIT; trademark (Winamp name/imagery) stays off-limits. Desktop wrappers exist (desktop.webamp.org; durasj/webamp-desktop PoC).
- **Qmmp / Audacious** — GPL, the two production Classic-skin players, **X11-only for the skinned UI**; Audacious docs: "Winamp interface is not supported on Wayland. Please run Audacious via XWayland" ([problems page](https://audacious-media-player.org/problems)); Audacious 4.5 shipped a Winamp 2.9 skin (Jul 2025). Study-only (copyleft).
- **qtWasabi / qtamp** — Qt6, MIT engine, **Modern `.wal` skins only** (Classic is a roadmap item); fetches the WCL-licensed Maki VM at build time; tiny traction (~1★/3★) but proves Qt6 renders Wasabi-era skins pixel-exactly on Wayland/HiDPI.

## 3. Rankings (from the report)

**Speed to faithful classic skins:** 1) Electron+Webamp+Butterchurn 2) Qt 6 3) Avalonia+shims 4) Tauri 5) JavaFX 6) Godot 7) Flutter.
**Long-term native quality:** 1) Qt 6 2) Rust/native custom + ported skin engine 3) Electron+Webamp.
**Flutter verdict: no** — no masks, no engine transparency, experimental multi-window, weak GL interop.
**Avalonia verdict: conditionally defensible only** — the SetWindowRgn-against-Skia-swapchain question is unproven anywhere; treat as a 2-3 day spike, not a plan.

## 4. Decision inputs this changes in the project docs

1. The Wayland caveat applies to **every** candidate equally (alpha-cap only on native Wayland; XWayland for masks) — so it is *not* a reason to abandon Avalonia specifically.
2. Avalonia's real deficits vs Qt: no mask API (Qt has setMask), no click-through, and the unproven GPU-clip interaction — these are the spikes to run if we stay on .NET.
3. Electron+Webamp inherits a proven 100k-skin renderer + Butterchurn, at the cost of a JS skin engine, an overlay-first window model, and a Chromium maintenance profile.
4. Qt 6 is the ceiling for fidelity and native quality; cost is building/porting a classic-skin engine (copyleft references only for study) and LGPLv3 dynamic-link discipline.

*This report is evidence for the recommendation delivered in chat; spec.md remains Avalonia-based pending the user's framework decision.*
