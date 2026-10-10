# Winamp-like Music Player on Avalonia: Platform & Clean-Room Research (state of the art, 2026-10-10)

Scope: feasibility evidence for a clean-room, Win/macOS/Linux Winamp-style player on Avalonia (.NET) with full Winamp 2/5 skin support, AVS/MilkDrop visualizations, 10-band EQ, local libraries, and Plex/Jellyfin integration. Every load-bearing claim carries a URL; versions, licenses, and release dates are stated per artifact; uncertain items are marked `[UNVERIFIED]` or `[INFERENCE]`.

---

## 0. Headline findings

1. **Avalonia is fit for purpose on desktop, with two hard caveats** — full per-pixel translucency is Windows-only; there is no native shaped/irregular window-region API. Both matter directly to a Winamp skin renderer.
2. **A ready-made cross-platform libmpv binding exists and is current**: `HanumanInstitute.LibMpv` **0.10.1** (MIT, published **2026-05-29**, `net10.0`/`netstandard2.0`) plus `HanumanInstitute.LibMpv.Avalonia` (depends on **Avalonia 12.0.3**), targeting Windows/macOS/Linux with GL/native/software renderers. This removes the biggest audio-integration risk.
3. **Custom-drawn visualizers are first-class**: `DrawingContext`, `ICustomDrawOperation` (raw SkiaSharp `SKCanvas`), `OpenGlControlBase`, composition GPU-image import, and a frame-rate-independent `TopLevel.RequestAnimationFrame` loop; 12.1 removed the 60 FPS cap on X11 and non-WinUIComposition Windows modes.
4. **Winamp classic (.wsz) skin format is fully documented and already reimplemented in a permissive project** (Webamp, MIT) — a legitimate clean-room reference for *format/behaviour*, not for code reuse from Winamp itself.
5. **The 2024 Winamp source release is legally toxic for a commercial product**: the "Winamp Collaborative License" forbids forking and distribution of modified versions entirely. Treat as contaminated; do not consult it.

---

## 1. Avalonia 11/12 fitness

### 1.1 Current version, cadence, licensing, vitality
- **Latest stable: Avalonia 12.1.4, published 2026-10-09, MIT license**; targets `net8.0` and `net10.0`. Recent line: 12.1.3 (2026-09-22), 12.1.2 (2026-09-02), 12.1.1 (2026-07-29), 12.1.0 (2026-07-09). Source: https://www.nuget.org/packages/Avalonia/
- **Avalonia 12.0 GA: 2026-04-07** ("1,007 days since 11.0", up to **1,867% FPS** in complex scenes, ~19 people full-time). Source: https://avaloniaui.net/blog/avalonia-12
- **11.x line is still maintained**: latest patch **11.3.22 (2026-09-11)**; 11.3.20 etc. Source: https://whatsnew.fyi/product/avalonia/latest
- NuGet 11.x examples target `net8.0` (e.g. Avalonia.Native 11.3.13). Source: https://www.nuget.org/packages/Avalonia.Native/11.3.13
- **Vitality**: high — quarterly+ patches, active blog, Devolutions sponsorship, EU CRA SBOM from 12.1.0. Sources: https://avaloniaui.net/blog/release-12-1 ; https://avaloniaui.net/blog/avalonia-12
- Prior art of Avalonia used at scale (31k+ stars): https://github.com/AvaloniaUI/Avalonia

### 1.2 Platform support matrix (12.x)
Official tiers (reviewed each major release): https://docs.avaloniaui.net/docs/supported-platforms

| Platform | Supported versions | Tier | Notes |
|---|---|---|---|
| Windows | 11 24H2 (26100) | 1 | Win32 API directly; Skia→D3D11 via ANGLE |
| Windows | 11 22H2, 10 22H2 (19045) | 2 | |
| Windows | earlier builds (incl. **Win7**) | 3 | "Issues closed, PRs not reviewed" — effectively **unsupported** |
| macOS | 26 Tahoe | 1 | Own Objective-C++ backend (no .NET macOS workload) |
| macOS | 15 Sequoia, 14 Sonoma | 2 | |
| macOS | **13 and earlier (incl. 10.15)** | 3 | unsupported |
| Linux | Ubuntu 25.x, Fedora 43, Debian 13 | 1 | X11 direct; Wayland opt-in from 12.1 |
| Linux | Ubuntu 16.04–24.x, Fedora 30–42, Debian 9–12 | 2 | |
| Linux | Arch/Alpine/NixOS/Gentoo, etc. | 3 | musl requires custom `libSkiaSharp.so` |
| WASM | browsers | — | .NET 8+; Skia via CanvasKit |

Key answers to the explicit questions:
- **Win7?** No (Tier 3). **Win10+?** Yes (Tier 2, x64). **macOS 10.15+?** No — Tier 3 below macOS 13; realistically target macOS 14+. **macOS 12?** Not in the supported matrix (Tier 3).
- **Linux X11/Wayland?** X11 is the default and Tier-1; **native Wayland backend shipped in 12.1.0 as opt-in** (`Avalonia.Wayland` package + `UseWayland()`), still labelled *experimental*, not picked up by `UsePlatformDetect()`. Source: https://avaloniaui.net/blog/release-12-1 ; https://docs.avaloniaui.net/docs/platform-specific-guides/linux.md
- **glibc 2.17** floor for Skia; musl = custom build (Tier 3).

### 1.3 .NET version strategy
- **Avalonia 12 dropped .NET Framework and .NET Standard**; only **.NET 8+**, recommended **.NET 10**. Mobile requires .NET 10. Source: https://docs.avaloniaui.net/docs/avalonia12-breaking-changes
- **Desktop minimum .NET 8.0**; WASM 8.0; iOS/Android 10.0. Source: supported-platforms (above).
- **.NET lifecycle (authoritative)**: .NET 8 LTS **EOL 2026-11-10**; .NET 9 STS EOL 2026-11-10; **.NET 10 LTS (GA 2025-11-11) supported to 2028-11-14**; **.NET 11 GA 2026-11-10** (STS). Source: https://github.com/dotnet/core/blob/main/releases.md ; https://learn.microsoft.com/en-us/dotnet/core/whats-new/dotnet-11/overview
- **Recommendation**: target **.NET 10 (LTS)** now; plan a .NET 11 move after it stabilises. .NET 8 is effectively EOL in ~5 weeks from this report's date, so do not ship on it.

### 1.4 Rendering stack (Skia, backends)
- Windows: Skia on **Direct3D 11 via ANGLE**; `Win32RenderingMode` = `AngleEgl` (default) → `Wgl` → `Vulkan` → `Software`. `Win32CompositionMode` = `WinUIComposition` (Win10 17134+, acrylic + high refresh) → `DirectComposition` → `LowLatencyDxgiSwapChain` (low latency, **no transparency/blur**) → `RedirectionSurface` (fallback). Source: https://docs.avaloniaui.net/docs/platform-specific-guides/windows
- Linux: OpenGL via Skia (X11), EGL/OpenGL-ES (Wayland), software (framebuffer). Source: supported-platforms.
- macOS: own backend, Metal rendering (cleanup fixes in 12.0). Source: https://avaloniaui.net/blog/avalonia-12
- **Custom graphics hook**: `Win32PlatformOptions.CustomPlatformGraphics` (custom `ISkiaGpu`) exists but disables `RenderingMode` and restricts `CompositionMode` to `RedirectionSurface`/null. Source: Windows guide (above).

### 1.5 Per-pixel alpha transparency (the biggest caveat)
- **"Windows supports all `TransparencyLevelHint` values. It is the only platform with full transparency support. macOS supports only `Transparent`, and Linux support depends on the compositor."** Source: https://docs.avaloniaui.net/docs/platform-specific-guides/windows
- Levels: `Transparent` (Win7+), `AcrylicBlur` (Win10 1803+), `Mica` (Win11). Must set `Background="Transparent"`. Query actual with `ActualTransparencyLevel`. Set a **non-transparent fallback** — Windows suppresses transparency under battery saver / remote sessions. Source: same page; https://docs.avaloniaui.net/docs/fundamentals/top-level (also `TransparencyBackgroundFallback`).
- Known defects to plan around: `AcrylicBlur` carry-over bug (https://github.com/AvaloniaUI/Avalonia/issues/18915); **v12 regression where `ExtendClientAreaToDecorationsHint` breaks `TransparencyLevelHint="Transparent"`** producing a black window (https://github.com/AvaloniaUI/Avalonia/issues/21082); low-latency DXGI swap chain does not support transparency (https://github.com/AvaloniaUI/Avalonia/issues/20615).
- **Implication**: a "floating skin windows" look is reliably achievable on Windows; on macOS only fully-transparent (no blur) windows; on Linux it depends on the compositor (GNOME/KDE composited OK, others may show black/opaque). `[INFERENCE]` from the documented platform asymmetry — plan a designed opaque fallback skin.

### 1.6 Shaped / irregular windows and click-through
- Avalonia exposes **no native window-region (`SetWindowRgn`-style) API**; the framework's own hit-testing lets a control define its geometry, but a whole *irregular top-level window* must be emulated with per-pixel transparency. `[INFERENCE]` — the documented primitives are transparency + hit-testing; no public region API is documented.
- Winamp's own mechanism is **`region.txt`**: header `[Normal]`/`[Equalizer]`, `NumPoints`, `PointList` polygons, X∈[0,275], Y∈[0,116]; out-of-polygon pixels are clear; polygons auto-closed. Source: https://www.alpha-ii.com/Info/Region.html
- **To replicate**: set window background transparent, draw only skin pixels, and override hit-testing. Avalonia supports custom hit regions via `HitTest` override / `ICustomHitTest` and `IsHitTestVisible=false`; background regions with `Background=null` already do not hit-test. Sources: https://github.com/AvaloniaUI/avalonia-docs/blob/main/docs/graphics-animation/hit-testing.md ; https://docs.avaloniaui.net/api/avalonia/rendering/icustomhittest
- **Window-level click-through to the desktop** (transparent pixels passing clicks to another app) is **not built in** and has long-standing discussions/issues: #17304 (non-clickable transparent window), #4956 (nonclickable content), #13827, #11911 (pass-through transparency v11), #20865 (transparent fullscreen blocks clicks). Sources: https://github.com/AvaloniaUI/Avalonia/discussions/17304 ; https://github.com/AvaloniaUI/Avalonia/issues/4956 ; https://github.com/AvaloniaUI/Avalonia/discussions/13827 ; https://github.com/AvaloniaUI/Avalonia/discussions/11911 ; https://github.com/AvaloniaUI/Avalonia/discussions/20865 — expect to P/Invoke platform APIs (`WS_EX_TRANSPARENT`/layered on Win) for true per-pixel desktop click-through. Mark this as **engineering risk**, not a solved feature.

### 1.7 Custom drawing and input on Skia
- Override `Control.Render(DrawingContext)`; use `InvalidateVisual()`; `AffectsRender<T>()` for property-driven invalidation. Source: https://github.com/AvaloniaUI/avalonia-docs/blob/main/docs/graphics-animation/custom-rendering.md
- For raw SkiaSharp: implement `ICustomDrawOperation` and obtain `ISkiaSharpApiLeaseFeature` → `SKCanvas`. Package refs `Avalonia.Skia` + `SkiaSharp`. Source: same doc.
- `OpenGlControlBase` (`Avalonia.OpenGL.Controls`, namespace `Avalonia.OpenGL.Controls`) renders with OpenGL. Source: https://docs.avaloniaui.net/api/avalonia/opengl/controls/openglcontrolbase ; tutorial: https://github.com/danipen/AvaloniaOpenGLTutorial
- OpenTK-over-Avalonia helper: `SamboyCoding/OpenTKAvalonia` (community; macOS scale-factor issues reported — issue #5). Sources: https://github.com/SamboyCoding/OpenTKAvalonia ; https://github.com/SamboyCoding/OpenTKAvalonia/issues/5
- GPU interop for video/3D/visualizers: composition `ImportGpuImage`/`ImportGpuSemaphore` on a `CompositionDrawingSurface` (Vulkan/Metal handles). Source: custom-rendering doc (GPU interop section).
- Input on custom draw: handle pointer events on your control and do your own geometry hit-testing (recommended for many/graphic elements). Source: hit-testing doc (above).
- Caveat: `RenderTargetBitmap` uses software rendering and does **not** capture `OpenGlControlBase`/GPU-path controls. Source: custom-rendering doc.

### 1.8 High-refresh / frame-rate-independent animation
- `TopLevel.RequestAnimationFrame(Action<TimeSpan>)` — callback receives elapsed time since the animation system started; re-arm from within the callback for a continuous loop; described as the Avalonia equivalent of WPF `CompositionTarget.Rendering`. Source: https://github.com/AvaloniaUI/avalonia-docs/blob/main/docs/fundamentals/top-level.md ; https://api-docs.avaloniaui.net/docs/M_Avalonia_Controls_TopLevel_RequestAnimationFrame
- **12.1 removed the 60 FPS cap**: X11 detects max monitor refresh and matches it; same for Windows when not using `WinUIComposition`. So 120/144/165 Hz rendering is supported. Source: https://avaloniaui.net/blog/release-12-1
- For render-thread work that must not block the UI thread, use `CompositionCustomVisualHandler`. Source: custom-rendering doc.
- `[INFERENCE]` For a music visualizer, drive the loop with `RequestAnimationFrame` (UI-thread, time-based), and for heavy GL scenes render on the render thread / OpenGL path.

### 1.9 DPI and pixel-perfect retro bitmaps
- Default Windows DPI mode is **`PerMonitorDpiAware`**; layout is in device-independent pixels; `Screen.Scaling` reports per-monitor scale (1.0/1.25/1.5/2.0). @2x assets auto-selected. Sources: https://docs.avaloniaui.net/docs/platform-specific-guides/windows ; https://docs.avaloniaui.net/docs/fundamentals/top-level (`RenderScaling`).
- **Interpolation**: `RenderOptions.BitmapInterpolationMode` = `None` | `LowQuality` (default, since 11) | `MediumQuality` | `HighQuality`. For a crisp Winamp look use **`None` + `RenderOptions.EdgeMode="Aliased"`**. Note this replaced WPF's `BitmapScalingMode` naming. Source: https://github.com/AvaloniaUI/avalonia-docs/blob/main/docs/graphics-animation/image-interpolation.md
- **Pixel-grid risk**: at fractional DPI scales (1.25/1.5) a 275×116 bitmap-sprite UI cannot land on integer device pixels without deliberate snapping. `[INFERENCE]` Mitigations: render the whole skin into one canvas at 1× and apply an integer scale transform, or set `DpiAwareness=SystemDpiAware`/`Unaware`, or snap via `LayoutHelper`/`PixelSize` rounding. The docs do not describe a dedicated pixel-snapping facility, so treat exact pixel-grid alignment at non-100% scaling as an **implementation risk**.

### 1.10 Visualizer sample apps / references in Avalonia
- `danipen/AvaloniaOpenGLTutorial` — 22-step Avalonia + OpenGL tutorial incl. `OpenGlControlBase` usage. https://github.com/danipen/AvaloniaOpenGLTutorial
- `DigitalBox98/Avalonia-OpenTK-Sample`. https://github.com/DigitalBox98/Avalonia-OpenTK-Sample
- `wieslawsoltes/MediaPlayer` — cross-platform Avalonia media player control (MIT). https://github.com/wieslawsoltes/MediaPlayer
- `LibMpv.Avalonia` (see §2) ships a working `MpvVideoView`. https://github.com/mysteryx93/LibMpv-OpenGL
- `[UNVERIFIED]` No first-party Avalonia "AudioVisualizer" sample was found; the OpenGL tutorial repos and LibMpv sample are the closest.

---

## 2. Avalonia + native libraries

### 2.1 libmpv bindings (.NET)
- **`HanumanInstitute.LibMpv` 0.10.1** — MIT, authors Charland/Beloborodov/Baxter, **published 2026-05-29**, targets `net10.0` + `.NETStandard2.0`. Project: https://github.com/mysteryx93/LibMpv-OpenGL ; package: https://www.nuget.org/packages/HanumanInstitute.LibMpv/0.10.1
- **`HanumanInstitute.LibMpv.Avalonia` 0.10.1** — MIT, **depends on Avalonia 12.0.3** and ReactiveUI.Avalonia; README states support for **Windows, macOS, Linux** with `MpvVideoView`, renderers: Windows native + AngleEGL (zero-copy yes), macOS OpenGL (no zero-copy), Linux OpenGL+EGL (zero-copy yes), software fallback. Requires **libmpv ≥ 0.40.0**. Sources: https://www.nuget.org/packages/HanumanInstitute.LibMpv.Avalonia/ ; https://raw.githubusercontent.com/mysteryx93/LibMpv-OpenGL/main/README.md
- **Native libmpv is NOT on NuGet** (mpv publishes no official prebuilt packages). Windows: `shinchiro/mpv-winbuild-cmake` or `zhongfly/mpv-winbuild` (`libmpv-2.dll`); macOS: `brew install mpv`; Linux: `libmpv-dev` (Ubuntu PPA recommended for ≥0.40). Source: LibMpv README (above).
- Other, weaker options: `hudec117/Mpv.NET-lib-` (WinForms/WPF, Windows), `mpvnet-player/mpv.net` (**Windows-only**, Windows 10+, .NET 10), old `jinyuttt` `LibMpv.Avalonia` 1.0.0 (2024-01-07, Avalonia 11.0.4, stale), deprecated `Mpv.NET` NuGet (unlisted, "legacy, no longer maintained"). Sources: https://github.com/hudec117/Mpv.NET-lib- ; https://github.com/mpvnet-player/mpv.net ; https://www.nuget.org/packages/LibMpv.Avalonia ; https://www.nuget.org/packages/Mpv.NET
- **No Avalonia sample in dotnet/avalonia official repos** uses mpv; `mpv-examples/libmpv` shows C# P/Invoke + native-window embedding on Windows only. Source: https://github.com/mpv-player/mpv-examples/tree/master/libmpv `[UNVERIFIED]` beyond that.

### 2.2 libmpv license (matters for a closed commercial app)
- mpv core is **LGPL-2.1-or-later** (some components GPL). `Copyright`/`LICENSE.LGPL` in the repo; ongoing "Relicense X11 as LGPL" discussion. Sources: https://github.com/mpv-player/mpv/blob/master/Copyright ; https://github.com/mpv-player/mpv/discussions/17356 ; https://github.com/mpv-player/mpv/blob/master/LICENSE.LGPL
- `[INFERENCE]` **Dynamic linking** to libmpv from a proprietary app is the standard LGPL-compliant pattern (ship the .dll/.dylib/.so separately, allow replacement); **static linking** would impose LGPL obligations. Confirm with counsel; the LGPL text itself is the authority: https://www.gnu.org/licenses/lgpl-2.1.html

### 2.3 Alternative audio/media stacks
- **LibVLCSharp + LibVLCSharp.Avalonia** — MIT bindings (package 3.10.1, `net8.0`/`netstandard2.0`), VLC under **LGPL-2.1**; built-in `Equalizer` class (support for VLC's EQ). Sources: https://www.nuget.org/packages/LibVLCSharp.Avalonia/3.10.1 ; https://docs.videolan.me/libvlcsharp/api/LibVLCSharp.Shared.Equalizer.html ; https://github.com/videolan/libvlcsharp
- **BASS + ManagedBass** — BASS is proprietary: free for freeware/non-commercial, **commercial license required** for commercial apps; `ManagedBass` (the .NET wrapper) is MIT. Sources: https://www.un4seen.com/license.php?l=1 ; https://github.com/managedbass/managedbass
- **NAudio** — the canonical .NET audio library, but **not truly cross-platform** (Linux/macOS support remains an open issue); **v3.1.0 on NuGet**. Sources: https://www.nuget.org/packages/NAudio/ ; https://github.com/naudio/NAudio/issues/1077
- **10-band EQ reality**: ffmpeg/mpv expose `equalizer`, `anequalizer`, `superequalizer` audio filters (`af`) — mpv can drive these for a 10-band EQ. Sources: https://mpv.io/manual/stable/ ; https://github.com/mpv-player/mpv/blob/master/DOCS/man/af.rst ; https://ffmpeg.org/doxygen/9.0/af__superequalizer_8c.html ; VLC/LibVLCSharp `Equalizer` (above).
- **Metadata/tags**: `mono/taglib-sharp` (LGPL-2.1) or the newer MIT alternative `decriptor/TagLibSharp2`. Sources: https://github.com/mono/taglib-sharp ; https://github.com/decriptor/TagLibSharp2/

### 2.4 Native dependency distribution & .NET publishing
- SkiaSharp and Avalonia native libs ship as NuGet **RID-specific runtime assets** (`runtimes/<rid>/native`); your own native deps (libmpv, projectM) must be packaged the same way or installed system-wide.
- **Native AOT** is supported by Avalonia; docs recommend `x:CompileBindings=True`, embedded resources (`AvaloniaResource`), avoid dynamic XAML/reflection DI, and add a `TrimmerRootAssembly` root descriptor; known limits: dynamic control creation, some third-party controls may not be AOT-compatible. Source: https://github.com/AvaloniaUI/avalonia-docs/blob/main/docs/deployment/native-aot.md
- **P/Invoke caveat**: managed P/Invoke itself is AOT- and trim-safe, but native shared libraries still need to be deployed alongside the exe; single-file self-contained generally still emits native .dll/.so/.dylib in the output (documented community observation: https://stackoverflow.com/questions/77643721/ ). `[INFERENCE]` For libmpv, prefer RID-specific content copying over \<SingleFile\> embedding. Trim warnings to watch: Avalonia IL2026/IL2104 (https://github.com/AvaloniaUI/Avalonia/issues/13897).
- **Recommended release shape**: framework-dependent multi-RID packages + self-contained/NativeAOT as an option; keep native libs side-by-side, not statically linked (also resolves LGPL).

---

## 3. Winamp UI semantics to replicate

### 3.1 Multi-window layout, snapping, docking
- Classic Winamp = three docked top-level windows: **main (275×116)**, equalizer, playlist (resizable), each snapping to the others at fixed offsets, with windowshade (collapsed) states.
- Reference algorithm (permissive): **Webamp** implements the same in a `windows` reducer with snapping/docking logic (MIT). Sources: https://github.com/captbaritone/webamp ; https://app.unpkg.com/webamp@2.3.1/files/built/types/js/reducers/windows.d.ts ; window-layout API discussion: https://github.com/captbaritone/webamp/issues/783
- Winamp's own DLL `DOCK.cpp`/`embwnd.cpp` exist in the 2024 source but are under the restrictive WCL (see §4) — **do not read**.
- **Avalonia feasibility**: model each Winamp window as a separate `Window` with `WindowDecorations="None"`, `Background="Transparent"`, `TransparencyLevelHint=[Transparent]`, `ShowInTaskbar` set only for the main window, and a managed `WindowSnapper` service computing offsets from the position/size of siblings on pointer-release/drag. Native drag via `BeginMoveDrag` or `WindowDecorationProperties.ElementRole="TitleBar"`. Sources: https://docs.avaloniaui.net/docs/platform-specific-guides/windows ; https://docs.avaloniaui.net/docs/fundamentals/top-level ; https://docs.avaloniaui.net/docs/how-to/window-how-to ; macOS MoveDrag bug fallback: https://github.com/AvaloniaUI/Avalonia/pull/21827
- **Single window + regions alternative**: possible (one transparent window, internally composited panes) but then z-order/independent monitormoves and taskbar semantics are lost; the multi-`Window` approach is closer to Winamp and is what the platform supports best. `[INFERENCE]`

### 3.2 Classic skin format (Winamp 2.x, `.wsz`)
- `.wsz` is a **ZIP** containing BMP sprite sheets + config text files. Source: https://jordaneldredge.com/blog/how-winamp2-js-loads-native-skins-in-your-browser/
- Documented sprites and geometry (Unofficial Winamp Skin Specifications v1.2.1, 1998): `main.bmp` **275×116**, `titlebar.bmp` **344×87** (buttons, titlebar, shade/unshade titlebars, clutterbar), `cbuttons.bmp`, `posbar.bmp`, `volume.bmp`, `shufrep.bmp`, `playpaus.bmp`, `numbers.bmp`, `text.bmp`, `monoster.bmp`; hot-area tables give exact coordinates. Source: https://raw.githubusercontent.com/WACUP/Winamp-Skinning-Archive/master/Classic%20Skins/skinspecs.pdf
- `viscolor.txt`: 24 RGB triples — #0 background, #1 dots, **#2–17 spectrum analyzer**, **#18–22 oscilloscope**. Source: skinspecs.pdf (above).
- `pledit.txt` (playlist colors), `region.txt` (shaped windows, see §1.6). Docs mirror: https://github.com/WACUP/Winamp-Skinning-Archive
- **Authoritative-behaviour cross-check (clean-room-safe)**: Webamp is a pixel-exact classic-skin renderer (MIT code) and hosts the **Winamp Skin Museum** of ~100k skins. Sources: https://docs.webamp.org/docs/features/skins ; https://skins.webamp.org/

### 3.3 Modern skins (Winamp 5, `.wal`)
- Modern skins are **XML GUI definitions + PNG assets + MAKI scripts compiled to a bytecode VM**; there is a scripting API surface. Sources: https://wiki.winamp.com/wiki/Modern_Skin:_Skin_Scripting ; https://wiki.winamp.com/wiki/XML_GUI_Objects ; https://wiki.winamp.com/wiki/Modern_Skin:_Maki_Overview
- Webamp maintains a **webamp-modern** prototype (MAKI bytecode reverse-engineering notes). Sources: https://github.com/captbaritone/webamp/tree/master/packages/webamp-modern ; https://github.com/captbaritone/webamp/blob/master/packages/webamp-modern/src/maki/maki-bytecode.md ; https://jordaneldredge.com/webamp-modern/
- **Assessment**: full Winamp 5 modern-skin compatibility requires reimplementing a **MAKI VM, XML layout engine, and skin script API** — a project of comparable magnitude to the player itself. Recommend scoping "full compatibility" to **classic 2.x now, modern 5.x as a later major milestone**; state this explicitly in the spec. `[INFERENCE]`

### 3.4 Feature checklist and Avalonia mapping
| Winamp behaviour | Avalonia mechanism | Status |
|---|---|---|
| Borderless skinned main/EQ/playlist windows | `WindowDecorations=None`, `Background=Transparent`, per-window `TransparencyLevelHint` | Supported (with transparency caveats §1.5) |
| Drag window by skin titlebar | `WindowDecorationProperties.ElementRole=TitleBar` or `BeginMoveDrag` | Supported |
| Windowshade (collapse with alt titlebar sprites) | Managed resize to shade height, swap sprite set | Supported (managed) |
| Snapping/docking | Managed offsets service on drag/resize | Supported (custom code) |
| `region.txt` shaped windows | Transparency + custom hit-test; desktop click-through needs P/Invoke | Partial / risk |
| 10-band EQ + presets (`.eqf`) | ffmpeg `superequalizer`/`equalizer` via mpv, or VLC `Equalizer`; `.eqf` parsing permissively documented (`winamp-eqf`, MIT) | Supported |
| Jump-to-file / search | Managed UI | Supported |
| Global hotkeys (system-wide, incl. hidden window) | **No built-in Avalonia API**; use **SharpHook** (cross-platform) or Win32 `RegisterHotKey` | Supported via 3rd party |
| Tray icon + menu | Avalonia `TrayIcon` control (native menus) | Supported |
| Taskbar integration | `ShowInTaskbar`, `TryGetPlatformHandle()` for HWND work | Supported |
| MMB scroll on volume/seek, drag-drop | Pointer events; drag-drop supported (X11 XDND cross-process added in 12.1) | Supported |
| Multi-monitor DPI | `PerMonitorDpiAware` default | Supported (pixel-snap caveat §1.9) |
Sources: Avalonia windows/top-level/tray/hit-testing docs (above); TrayIcon https://docs.avaloniaui.net/controls/navigation/trayicon ; SharpHook https://sharphook.tolik.io/ ; global-hotkey gap discussions https://github.com/AvaloniaUI/Avalonia/discussions/8823 , /14413, /16266 ; `.eqf` parser https://github.com/captbaritone/webamp/tree/master/packages/winamp-eqf ; drag-drop https://avaloniaui.net/blog/release-12-1

---

## 4. Clean-room methodology

### 4.1 The defensible clean-room model
- **Two-team "spec-from-behaviour"**: a *specification team* may study the original (including behaviour, docs, and — where lawful — binaries) and writes a functional specification; an *implementation team* writes code **solely from that spec and never sees the original**. Document the isolation (access logs, signed statements, dated spec revisions). This is the classic defense established in the reverse-engineering cases below.
- **What is safe to derive**: interfaces, file formats, observable behaviour, protocol wire formats, and functional requirements. **What is not**: literal code, comments, identifiers, and creative expression copied from the original.

### 4.2 US case law (behaviour-derived clean rooms)
- **Sega v. Accolade, 977 F.2d 1510 (9th Cir. 1992)** — intermediate copying of Sega object code to discover the console's compatibility requirements (a functional, unprotected interface) was **fair use**. Sources: https://law.justia.com/cases/federal/appellate-courts/F2/977/1510/305345 ; https://www.copyright.gov/fair-use/summaries/segaenters-accolade-9thcir1992.pdf
- **Sony v. Connectix, 203 F.3d 596 (9th Cir. 2000)** — intermediate copying of the PlayStation BIOS to reverse-engineer it was **fair use**; the shipped emulator contained no Sony code. Source: https://copyright.gov/fair-use/summaries/sony-connectix-9thcir2000.pdf
- **Google v. Oracle, 593 U.S. ___ (2021)** — the Supreme Court held Google's reimplementation of Java's declaring code / API was **fair use**. Source: https://www.supremecourt.gov/opinions/20pdf/18-956_d18f.pdf ; summary: https://www.copyright.gov/fair-use/summaries/Google-LLC-v-Oracle-Am-Inc-141-S-Ct-1183-2021.pdf
- **DMCA §1201 caveat** — even lawful reverse engineering does not license circumventing technological protection measures; keep the project free of any copy-protection circumvention. Source: https://www.everycrsreport.com/reports/RL32692.html
- `[INFERENCE]` These are US precedents; a commercial product sold in the EU should also respect the EU Software Directive's interoperability provisions — but this analysis is jurisdiction-specific and needs qualified counsel. Treat the above as persuasive, not a guarantee.

### 4.3 License contamination: GPL/AGPL vs MIT/BSD for a closed app
- **GPL**: distributing a derivative/combined work requires the whole under GPL; the FSF's position is that even **dynamic linking** to a GPL library generally creates a combined work — so GPL libraries are unusable in a closed product. Source: https://www.fsf.org/blogs/licensing/licensing-and-compliance-lab-the-most-frequently-asked-frequently-asked-questions ; discussion: https://opensource.stackexchange.com/questions/2157
- **LGPL**: explicitly permits **dynamic linking** into proprietary software (the standard use case); static linking pulls in GPL-style obligations. Authorities: https://www.gnu.org/licenses/lgpl-2.1.html
- **AGPL**: adds a **network-use** copyleft trigger — a network service exposing AGPL code must offer source. FSF FAQ: https://www.gnu.org/licenses/gpl-faq.html
- **MIT/BSD/Apache**: safely reusable in closed source with attribution; Apache-2.0 adds a patent grant. `[INFERENCE]` from the standard license texts.
- **Practical rule for this project**: allowed = MIT/BSD/Apache (Webamp, LibMpv, Jellyfin.Sdk, MediaPlayer, projectm-eval); allowed-with-disclosure = LGPL dynamically linked (libmpv, libVLC, taglib-sharp); forbidden = GPL/AGPL code (projectM core is **LGPL-2.1** — check: rivy/projectM lists LGPL-2.1, https://github.com/rivy/projectM — so dynamic linking OK, but verify each projectM dependency's GPL parts).

### 4.4 The Winamp source release is a trap (critical)
- On 2024-09-24 Llama Group open-sourced the legacy player under a bespoke, contradictory **"Winamp Collaborative License (WCL) v1.0"**. Key terms: you may view/study and **modify for private use only**; **"No Distribution of Modified Versions"**, **"No Forking"**, only maintainers distribute; contributions assign rights to Winamp; governed by **Belgian law**, exclusive Brussels jurisdiction. Source (full text): https://raw.githubusercontent.com/ErikMcClure/bad-licenses/master/winamp.md
- Context/news: the repo was published then deleted after controversy (Ars Technica 2024-10; BetaNews 2024-10). Sources: https://arstechnica.com/gadgets/2024/10/winamp-really-whips-open-source-coders-into-frenzy-with-its-source-release ; https://betanews.com/article/winamp-source-code-vanishes-from-hithub ; https://www.hackster.io/news/llama-group-releases-the-winamp-source-code-but-beware-its-restrictive-license-189e1b933c42 ; https://www.heise.de/en/news/Winamp-Source-code-disclosed-Discussions-about-license-9954017.html ; encyclopedia: https://en.wikipedia.org/wiki/Winamp
- **Consequence**: WCL is *not* a free/open license, not GPL, and not MIT — it forbids shipping a forked/modified player. Reading it can only contaminate the clean room. **Do not consult Winamp source, Winamp DLLs, or WCL-covered assets**; do not use the Winamp name/llama trademark (WCL §13; trademark remains with the owner). Source: https://www.llama-group.com/companies/winamp
- **Skin format IP**: the *format* (a ZIP of BMPs + text files) and the coordinate tables are functional/factual; user-created `.wsz` files are third-party artworks with their own (often absent) licenses. Redistributing skins is a separate IP question from reading the format. `[INFERENCE]`; the freeware/documented spec (Strider 1998 / WACUP archive) is the safe knowledge source.

---

## 5. Prior art

### 5.1 Winamp reimplementations (cross-checking references)
| Project | Language/stack | License | Notes / vitality |
|---|---|---|---|
| **Webamp** (`captbaritone/webamp`) | HTML5/JS/TS | **MIT** (code; Winamp name/interface = Nullsoft property) | Full classic-skin renderer, EQ `.eqf`, skin museum (~100k skins); reference for skin parsing + window manager | 
| winamp2-js | JS | MIT | Predecessor of Webamp; the original skin-parser blog |
| **wamp** (`wishval/wamp`) | Swift/macOS | (see repo) | "Classic Winamp 2.x, reborn as a native macOS app"; loads real skins |
| Llama Whip | (macOS player) | `[UNVERIFIED]` | Referenced as companion of `dragontpe/skin-forge`; Winamp player for macOS |
| cliamp (`bjarneo/cliamp`) | TUI (Rust/Go?) | MIT | 4.4k stars; "inspired by Winamp" (terminal) |
| Winamp (Llama) community fork | C++ | WCL (non-free) | **Do not use** |
Sources: https://github.com/captbaritone/webamp ; https://docs.webamp.org/docs/features/skins ; https://github.com/wishval/wamp ; https://github.com/dragontpe/skin-forge ; https://github.com/bjarneo/cliamp

### 5.2 .NET / Avalonia music players & media controls
- `wieslawsoltes/MediaPlayer` — cross-platform Avalonia media control, **MIT**. https://github.com/wieslawsoltes/MediaPlayer
- `AvaloniaUI/Avalonia.MusicStore` — official Avalonia sample (old, 2 commits since 2021) — reference for MVVM, not playback. https://github.com/AvaloniaUI/Avalonia.MusicStore
- `WangWindow/AvaPlayer`, `ibram-reda/Avalonia.MediaPlayer`, `jojomondag/FFmpegVideoPlayer.Avalonia`, `crucifix86/AvaloniaMediaPlayer` — community Avalonia players (activity/licence varies). https://github.com/WangWindow/AvaPlayer
- `LibMpv.Avalonia` sample (mysteryx93) — the strongest playing reference (Avalonia 12, Win/mac/Linux). https://github.com/mysteryx93/LibMpv-OpenGL
- `[UNVERIFIED]` No mature **skinned Winamp-clone in C#/Avalonia** was found — this is an open niche.

### 5.3 Visualization engines (AVS / MilkDrop)
- **projectM** — cross-platform, MilkDrop-compatible reimplementation (C/C++, rendering via OpenGL). Repo/license: https://github.com/projectM-visualizer/projectm (rivy fork lists **LGPL-2.1**: https://github.com/rivy/projectM). Site: https://projectm-visualizer.org/
- **projectm-eval** — MIT, drop-in replacement for MilkDrop's `ns-eel2` expression parser. https://github.com/projectM-visualizer/projectm-eval
- **Butterchurn** — JS/WebGL MilkDrop 2 implementation (used by Webamp), MIT. https://github.com/jberg/butterchurn ; Webamp integration: https://github.com/captbaritone/webamp
- **AVS** (Advanced Visualization Studio) is a *separate* legacy Winamp plugin format, not MilkDrop. Open-source handling/decoders exist: `grandchild/vis_avs`, `grandchild/avs-file-decoder`, `Const-me/vis_avs_dx`, `hinjolicious/webvs`. Sources: https://github.com/grandchild/vis_avs ; https://github.com/grandchild/avs-file-decoder ; https://github.com/Const-me/vis_avs_dx ; https://github.com/hinjolicious/webvs
- `[INFERENCE]` For a new player: ship **projectM (dynamic LGPL)** for MilkDrop and treat AVS as a *preset-compatibility* effort (open decoders exist but there is no maintained .NET AVS runtime); AVS was historically the default Winamp plugin, so label AVS support as a stretch milestone.

### 5.4 Plex / Jellyfin integration
- **Jellyfin**: `Jellyfin.Sdk` — **MIT**, Kiota-generated, targeting net6/net8/netstandard; stable 2025.10.21, nightlies through 2026.10.10. Sources: https://www.nuget.org/packages/Jellyfin.Sdk/2025.10.21 ; https://github.com/jellyfin/jellyfin-sdk-csharp ; API overview: https://jellyfin-jellyfin.mintlify.app/development/api-overview
- **Plex**: `LukeHagar.PlexAPI.SDK` **0.21.0** (2026-07-19) — OpenAPI-generated for the Plex Media Server API + plex.tv (X-Plex-Token auth, OAuth PIN flow, XML/JSON). Sources: https://www.nuget.org/packages/LukeHagar.PlexAPI.SDK ; https://developer.plex.tv/ ; other clients: https://github.com/jensenkd/plex-api , https://github.com/soenneker/soenneker.plex.openapiclient
- Playback pattern: fetch a direct/transcoded stream URL from the server, hand it to libmpv/VLC — no vendor SDK playback required. `[INFERENCE]`

---

## 6. Risks, gaps, and recommended stack

**Ranked risks**
1. **Linux transparency + shaped windows** — compositor-dependent; no native region API; window click-through needs platform P/Invoke. Prototype on GNOME/KDE early; design an opaque fallback skin.
2. **Winamp 5 modern skins** — requires a MAKI VM + XML engine; scope to classic 2.x first.
3. **Pixel-grid alignment at fractional DPI** — no documented snapping facility; needs an explicit render-at-1×+integer-scale strategy.
4. **libmpv deployment** — no NuGet native package; per-OS sourcing and LGPL dynamic-linking compliance.
5. **Global hotkeys** — not built into Avalonia; add SharpHook / platform APIs.
6. **v12 transparency regressions** — pin a known-good patch and add visual regression tests (issues #21082, #18915).

**Recommended stack (all verified current)**
- UI/rendering: **Avalonia 12.1.x** (MIT) on **.NET 10 LTS**; Skia backends default; custom `ICustomDrawOperation` for skin canvas, `OpenGlControlBase`/GPU-image import for visualizers.
- Audio/media: **libmpv** via `HanumanInstitute.LibMpv` + `.LibMpv.Avalonia` (MIT, LGPL native, dynamic); fallback `LibVLCSharp` (LGPL). Avoid BASS unless a commercial license is bought; avoid NAudio for cross-platform.
- EQ: ffmpeg `superequalizer`/`equalizer` (mpv `af`) or VLC `Equalizer`; `.eqf` parsing per `winamp-eqf`.
- Metadata: `taglib-sharp` (LGPL, dynamic) or `TagLibSharp2` (MIT).
- Servers: `Jellyfin.Sdk` (MIT), `LukeHagar.PlexAPI.SDK` (0.21.0).
- Visualization: **projectM** (MilkDrop, LGPL dynamic) + `projectm-eval` (MIT); AVS as a later milestone.
- Hotkeys: **SharpHook**; Tray: built-in `TrayIcon`.
- Skin reference: **Webamp docs + MIT code** for format/behaviour only; **never** Winamp/WCL source.
- Licensing/legal: strict two-team clean room; LGPL dynamic linking only; MIT/minimal attribution for the rest; trademark avoidance for the Winamp name.

---

## Appendix — Source list (with type/date where relevant)
Avalonia: https://www.nuget.org/packages/Avalonia/ · https://docs.avaloniaui.net/docs/supported-platforms · https://docs.avaloniaui.net/docs/avalonia12-breaking-changes · https://docs.avaloniaui.net/docs/platform-specific-guides/windows · https://docs.avaloniaui.net/docs/fundamentals/top-level · https://github.com/AvaloniaUI/avalonia-docs/blob/main/docs/graphics-animation/{custom-rendering,hit-testing,image-interpolation}.md · https://docs.avaloniaui.net/api/avalonia/opengl/controls/openglcontrolbase · https://docs.avaloniaui.net/controls/navigation/trayicon · https://avaloniaui.net/blog/avalonia-12 · https://avaloniaui.net/blog/release-12-1 · https://whatsnew.fyi/product/avalonia/latest · https://github.com/AvaloniaUI/Avalonia/issues/{18915,21082,20615,13897,4956,15340,5116} · discussions {17304,13827,11911,20865,8823,14413,16266}
.NET: https://github.com/dotnet/core/blob/main/releases.md · https://learn.microsoft.com/en-us/dotnet/core/whats-new/dotnet-11/overview
Native/libmpv: https://github.com/mysteryx93/LibMpv-OpenGL · https://www.nuget.org/packages/HanumanInstitute.LibMpv/0.10.1 · https://www.nuget.org/packages/HanumanInstitute.LibMpv.Avalonia/ · https://github.com/mpv-player/mpv/blob/master/{Copyright,LICENSE.LGPL,DOCS/man/af.rst} · https://shinchiro/mpv-winbuild-cmake (GitHub) · https://github.com/videolan/libvlcsharp · https://www.un4seen.com/license.php?l=1 · https://www.nuget.org/packages/NAudio/ · https://github.com/mono/taglib-sharp
Winamp format: https://raw.githubusercontent.com/WACUP/Winamp-Skinning-Archive/master/Classic%20Skins/skinspecs.pdf · https://www.alpha-ii.com/Info/Region.html · https://jordaneldredge.com/blog/how-winamp2-js-loads-native-skins-in-your-browser/ · https://docs.webamp.org/docs/features/skins · https://wiki.winamp.com/wiki/{Modern_Skin:_Skin_Scripting,XML_GUI_Objects,Modern_Skin:_Maki_Overview}
Clean-room/legal: https://law.justia.com/cases/federal/appellate-courts/F2/977/1510/305345 · https://www.copyright.gov/fair-use/summaries/{segaenters-accolade-9thcir1992,sony-connectix-9thcir2000,Google-LLC-v-Oracle-Am-Inc-141-S-Ct-1183-2021}.pdf · https://www.supremecourt.gov/opinions/20pdf/18-956_d18f.pdf · https://www.everycrsreport.com/reports/RL32692.html · https://www.fsf.org/blogs/licensing/licensing-and-compliance-lab-the-most-frequently-asked-frequently-asked-questions · https://www.gnu.org/licenses/lgpl-2.1.html · https://www.gnu.org/licenses/gpl-faq.html · https://raw.githubusercontent.com/ErikMcClure/bad-licenses/master/winamp.md · https://arstechnica.com/gadgets/2024/10/winamp-really-whips-open-source-coders-into-frenzy-with-its-source-release
Prior art: https://github.com/captbaritone/webamp · https://github.com/wishval/wamp · https://github.com/bjarneo/cliamp · https://github.com/wieslawsoltes/MediaPlayer · https://github.com/projectM-visualizer/projectm · https://github.com/projectM-visualizer/projectm-eval · https://github.com/grandchild/vis_avs · https://www.nuget.org/packages/Jellyfin.Sdk/2025.10.21 · https://www.nuget.org/packages/LukeHagar.PlexAPI.SDK
