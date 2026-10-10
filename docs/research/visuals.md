# Winamp Visualization Compatibility - Research Report

Scope: clean-room, cross-platform (Win/macOS/Linux) Avalonia/.NET Winamp-like player. Focus: vis.h plugin ABI, MilkDrop/MilkDrop2, AVS, beat detection, and embedding projectM in Avalonia. Research current to 2026-10. All claims cited; uncertain items marked `[UNVERIFIED]`.

---

## 0. Bottom line (feasibility verdict)

| Goal | Verdict | Recommended base |
|---|---|---|
| Full **MilkDrop 1/2** preset playback | **Feasible, high fidelity** | `libprojectM` 4.x (LGPL-2.1-or-later) via P/Invoke; optionally Butterchurn (MIT) as reference |
| MilkDrop **3** (`.milk2` double presets, q1-q64, sprites) | **Mostly feasible** | libprojectM 4.2+ (sprites, `.milk` parser); MilkDrop3 extras partial |
| Full **AVS** compatibility | **Not practical** | Only `grandchild/vis_avs` `libavs` (BSD-3) as an embedded native lib; incomplete (no APEs, no 64-bit) |
| Loading **arbitrary `vis_*.dll`** in-process | **Windows-32-bit-only hobby path** | `LoadLibrary` + delegates; not embeddable in Avalonia; Wine on Linux/macOS |
| **10-band EQ** | out of scope here (Winamp uses its own DSP; not a visualization concern) | - |
| projectM **inside Avalonia** | **Feasible but platform-sensitive** | `OpenGlControlBase` (in-context callbacks) or FBO + `GpuInterop`; macOS needs forced OpenGL mode |

Key architectural conclusion: treat *visualizations* as pluggable backends fed by a single host audio/FFT feature provider. Ship **projectM** as the flagship MilkDrop backend; do **not** attempt to host third-party Win32 vis DLLs in the Avalonia visual tree.

---

## 1. Winamp vis plugin API (`vis.h`) - exact C ABI

### 1.1 The header

Authoritative source: Ryan Geiss's own BSD-3-Clause copy of `VIS.H` in the **geiss** repo ([raw](https://raw.githubusercontent.com/geissomatik/geiss/main/VIS.H)); an identical mirror is in [xmp-wavis `winamp_vis.h`](https://raw.githubusercontent.com/schellingb/xmp-wavis/master/winamp_vis.h). Winamp's own `vis.h` also now ships in the 2024 Winamp source dump (see 1.5).

```c
typedef struct winampVisModule {
  char *description;        // description of module
  HWND hwndParent;          // parent window (filled in by calling app)
  HINSTANCE hDllInstance;   // instance handle to this DLL (filled in by calling app)
  int sRate;                // sample rate
  int nCh;                  // number of channels
  int latencyMs;            // latency from call of RenderFrame to actual drawing
  int delayMs;              // delay between calls in ms

  // the data is filled in according to the respective Nch entry
  int spectrumNch;
  int waveformNch;
  unsigned char spectrumData[2][576];
  unsigned char waveformData[2][576];

  void (*Config)(struct winampVisModule *this_mod);  // configuration dialog
  int  (*Init)(struct winampVisModule *this_mod);    // 0 on success, creates window
  int  (*Render)(struct winampVisModule *this_mod);  // 0 ok, 1 = vis should end
  void (*Quit)(struct winampVisModule *this_mod);

  void *userData;
} winampVisModule;

typedef struct {
  int version;       // VIS_HDRVER
  char *description;
  winampVisModule* (*getModule)(int);
} winampVisHeader;

typedef winampVisHeader* (*winampVisGetHeaderType)();
#define VIS_HDRVER 0x101   // 1.01
```

Notes carried in the header itself: *"ints are 32 bits, and structure members are aligned on the default 8 byte boundaries; tested with VC++ 4.2 and 5.0"*, and config is expected in `<dll directory>\plugin.ini`.

So the plugin surface is exactly:
- **Export**: one C symbol `winampVisGetHeader()` returning `winampVisHeader*`.
- **Header**: `version` (0x101), `description`, and `getModule(index)` (index 0 = first/current module, others optional).
- **Module**: window/instance handles, format info, the two data buffers, four callbacks (`Config`, `Init`, `Render`, `Quit`), `userData`.
- **`Init` is expected to create its own window / GL context**; the host only supplies `hwndParent` and an `HINSTANCE`. (This is the crux for embedding - see 1.4.)

### 1.2 Buffer contract (`spectrumData`, `waveformData`) - what is actually true

The struct declares `[2][576]` **bytes** per buffer (2 channels x 576), values **0-255**. But the real Winamp implementation does **not** fill 576 usable bands. From Winamp's legacy source `classic_vis.cpp` ([Winamp source mirror](https://forge.light.kow.is/dkowis/winamp/src/branch/community/Src/Winamp/classic_vis.cpp)):

- `makeSpecData()` performs an FFT and writes a spectrum into **75 bands** (`for (int x = 0; x < 75; x++)`), scaling each band to 0-255. It uses a log-ish warp (`SAPOW(x)=2^(x/12)`) over FFT bins 0..255.
- `makeOscData()` decimates the PCM block into **75 waveform points** (`for (x = 0; x < 75; x++)`), range -32..32 stored in a byte (i.e. waveform byte values centered around 0, effectively -32..31 or similar).
- The buffers are therefore **75 values per channel** by convention; `576` is the maximum buffer size. This is explicitly acknowledged by the widely used `vis_classic` plugin: *"note that the magic value 576 is used everywhere, it's really a bar count limit"* ([WACUP/vis_classic](https://github.com/WACUP/vis_classic)).

Therefore:
- **"0-74"** (75 bands) is the *historically correct* per-channel usable count for the classic plugin ABI. `spectrumNch`/`waveformNch` tell the plugin how many of the two channels are valid (1 or 2).
- **"288 bands"** and **"576 samples"** as *usable* band counts: **`[UNVERIFIED]`**. I found no primary source describing 288 bands. 576 is the per-channel array *capacity*; 288 could arise from a 576-bytes / 2-bytes-per-value reading, but the SDK and Winamp source treat the values as **1 byte each**. Treat any "288 bands" claim as folklore until re-checked against a specific SDK revision.
- Values are unsigned bytes 0-255 for spectrum; waveform bytes are signed-ish (bias around 0) - plugin code should spectrums as 0..255 and treat waveform as centered.
- The newer Winamp "sa" interface (WACUP/`IPC_GETSADATAFUNC`) can deliver higher bar counts (up to 576 for "classic analyzer" style plugins like `vis_classic`), which is why some modern plugins assume 576 bands. This is an extension, not the base ABI.

Other useful fields: `sRate` (Hz), `nCh`, `latencyMs` (host accounts for render latency when selecting data), `delayMs` (requested render interval, e.g. 15/25/35 ms).

### 1.3 Can a .NET host P/Invoke-load arbitrary 32/64-bit vis DLLs?

Mechanically on Windows: yes-ish.
- `[DllImport]`/`NativeLibrary.Load` + `GetExport("winampVisGetHeader")`, marshal the returned struct with `Marshal.PtrToStructure`, and build managed delegates for `Config/Init/Render/Quit` (`Marshal.GetDelegateForFunctionPointer`), or `unsafe`/function-pointer (`delegate* unmanaged`) with `[UnmanagedCallersOnly]` for the data path. Structure layout must be pinned: `[StructLayout(LayoutKind.Sequential)]`, `[MarshalAs(UnmanagedType.ByValArray, SizeConst=576)] byte[]` for `spectrumData`/`waveformData` (2 rows), `IntPtr` for HWND/HINSTANCE.
- **Bitness is the first hard wall**: virtually all real Winamp vis plugins are **32-bit x86** DLLs (Winamp 2.x/5.x era, compiled with VC++ 4.2/5.0/6 and MASM). A 64-bit .NET host (`AnyCPU`/x64) cannot load them. You would need a 32-bit host process (or a 32-bit helper process/IPC).
- **Second wall**: `Init()` requires the plugin to create its own Win32 window and typically its own OpenGL/Direct3D context; `Render()` is host-driven at `delayMs`. There is no API to hand the plugin an Avalonia GL context or a texture. So even if loaded, you get a **separate OS window**, not an Avalonia visual. Embedding would require either reparenting the plugin HWND (fragile, needs `SetParent`/window subclassing) or region/DWM capture.
- **Third wall**: plugins assume Winamp-specific IPC (`SendMessage(hwndParent, WM_WA_IPC, ...)`, `wa_ipc.h`), `plugin.ini` in the plugin dir, and often the Winamp main window as `hwndParent`. Many will misbehave standalone (see xmp-wavis, which re-implements a fake `hwndParent` + WASABI service just to host vis DLLs: [xmp-wavis.cpp](https://raw.githubusercontent.com/schellingb/xmp-wavis/master/xmp-wavis.cpp)).

Conclusion: hosting arbitrary vis DLLs is a **Windows-only, 32-bit, out-of-process** feature at best. It is not a portable or clean-room-friendly path.

### 1.4 vis on Linux/macOS

- Winamp vis plugins are **PE/Windows DLLs** (Win32 + HWND + GDI/OpenGL/DirectX). There is **no native loader** on Linux/macOS. The only ways to run them are **Wine** (32-bit prefix) - e.g. the grandchild AVS project documents running `vis_avs.dll` under Wine ([vis_avs README](https://github.com/grandchild/vis_avs)) - or a VM.
- Linux has its own, *different* plugin ecosystem: **libvisual** ([libvisual.org](https://libvisual.org/), [Libvisual/libvisual](https://github.com/Libvisual/libvisual)). It is not binary-compatible with vis.h; it's an actor/plugin abstraction used by XMMS/Audacious. `0.4.x` is maintenance/bugfix only; `master` targets 0.5.x (C11/C++20), not released as stable. License LGPL-2.1 `[UNVERIFIED exact SPDX]`. It is not a substitute for vis.h ABI compatibility.
- macOS never had a Winamp vis ABI. Any third-party Winamp visualizer must be re-implemented or run under Wine.

### 1.5 Winamp source availability (for clean-room caution)

Winamp's legacy source was released **2024-09-24** by Llama Group, then the repo was deleted amid licensing confusion ([Ars Technica](https://arstechnica.com/gadgets/2024/10/winamp-really-whips-open-source-coders-into-frenzy-with-its-source-release/), [Hackaday](https://hackaday.com/2024/09/24/winamp-releases-source-code-but-is-it-really-open/)). Mirrors include [manfromafar/winamp](https://github.com/manfromafar/winamp) and [dkowis/winamp (Forgejo)](https://forge.light.kow.is/dkowis/winamp). **Use these only as a behavioural reference**, not as code to copy: the release license is contested, and the historically BSD-licensed artifacts are (a) the original `vis.h` from Ryan Geiss (BSD-3) and (b) AVS (BSD-style, 2005). For clean-room, re-implement from the documented ABI above.

---

## 2. MilkDrop preset format (`.milk` / `.milk2`)

### 2.1 Structure

A `.milk` file is a text INI-like file. Real example ([projectM cream-of-the-crop preset](https://raw.githubusercontent.com/projectM-visualizer/presets-cream-of-the-crop/master/Particles/Grid/Stahlregen%20%26%20flexi%20%2B%20Geiss%20%2B%20Rovastar%20-%20Tides_3.milk)):

```
MILKDROP_PRESET_VERSION=201
PSVERSION=2
PSVERSION_WARP=2
PSVERSION_COMP=2
[preset00]
... static params: zoom, rot, cx, cy, dx, dy, warp, sx, sy, fDecay, wave_r/g/b, ob_*/ib_* (inner/outer border), mv_* (motion vectors), ...
wavecode_0_enabled=0 ... wavecode_0_samples=512 ...
shapecode_0_enabled=0 ... shapecode_0_sides=100 ... shapecode_0_textured=1 ...
per_frame_1=zoom = 1.0;
per_frame_2=wave_a = 0;
warp_1=`shader_body
warp_2=`{ ... ret = tex2D(sampler_main, uv).xyz; ... }
comp_1=`shader_body
comp_2=`{ ... }
```

Facts:
- One file may contain **multiple `[presetNN]` sections**; MilkDrop picks one/cycles (and MilkDrop3's `.milk2` blends two simultaneously).
- **Equation code** is stored line-by-line as `per_frame_N=`, `per_pixel_N=`, `per_frame_init_N=`, `wave_N=`/`wavecode_N_*`, `shape_N=`/`shapecode_N_*`, plus shader blocks `warp_1..` and `comp_1..`. Lines beginning with a backtick (`` ` ``) are shader code; lines without are EEL/nseel equation code (see 2.3).
- Authoritative format/asset documentation: Ryan Geiss's **MilkDrop Preset Authoring Guide** ([geisswerks](https://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html), mirror [HTML](https://raw.githubusercontent.com/afedchin/visualization.milkdrop2/master/visualization.milkdrop2/resources/Milkdrop2/docs/milkdrop_preset_authoring.html) and [milkdrop.org](https://milkdrop.org/resources/preset-authoring)). It documents per-frame variables (`zoom`, `rot`, `warp`, `decay`, `bass/mid/treb`, `bass_att/mid_att/treb_att`, `time`, `fps`, `frame`, `progress`, `meshx/meshy`, `pixelsx/pixelsy`, `aspectx`, motion vectors, borders), per-vertex code, custom shapes/waves, and the WARP/COMP shader model.
- **Per-frame vs per-pixel**: `per_frame` runs once/frame (CPU), `per_pixel` runs per mesh vertex (CPU, mesh size configurable, historically `meshx` 8-128, `meshy` 6-96), and GPU `warp`/`comp` are HLSL Shader Model 3.0-isms executed per pixel.
- **MilkDrop 2** added the pixel shaders (warp/comp, Shader Model 3.0); **MilkDrop 1** presets predate them. Version markers `MILKDROP_PRESET_VERSION`, `PSVERSION*` distinguish eras.
- A `.milk` preset is meaningless without its **texture pack** (`sampler_main`, `sampler_noise_*`, `sampler_pw_*`, blur textures, user sprite images). projectM ships/recommends the [base Milkdrop texture pack](https://github.com/projectM-visualizer/presets-milkdrop-texture-pack).

### 2.2 Preset vocabulary size

projectM's own list of preset packs ([README](https://github.com/projectM-visualizer/projectm)):
- Cream of the Crop: **~10K** presets (default pack).
- Classic projectM: **~4K** presets.
- Original MilkDrop/MilkDrop2 collections, En D (~50), plus a **130K+ MegaPack** (~4.08 GB incl. textures).
- MilkDrop3's own release adds ~500 presets ([Winamp forum](https://forums.winamp.com/forum/visualizations/milkdrop/milkdrop-presets/4614686-milkdrop-3-0-500-presets-lots-of-new-functionality)).

So "~4000 preset vocabulary" maps to the classic projectM pack; real-world corpus is 10K+ before the MegaPack.

### 2.3 Equation language (`nseel`/EEL)

Equations are compiled from Nullsoft's **ns-eel2** expression language (loops, `reg00..reg99`, `gmegabuf`/`megabuf`, `if()`, `$PI`, `$PHI`, bitwise ops, etc.). projectM reimplemented the compiler from scratch as **projectm-eval** (MIT? - check; the repo is [projectM-visualizer/projectm-eval](https://github.com/projectM-visualizer/projectm-eval)) with a documented [Expression-Syntax.md](https://github.com/projectM-visualizer/projectm-eval/blob/master/docs/Expression-Syntax.md), explicitly matching ns-eel2 semantics including quirks (`if()` returning lvalues, assignment to conditional expressions, undocumented constants).

---

## 3. Open-source MilkDrop engines

### 3.1 projectM / libprojectM - **the recommended backend**

- **Repo**: [projectM-visualizer/projectm](https://github.com/projectM-visualizer/projectm). **License**: core `libprojectM` is **GNU LGPL-2.1-or-later** ([README](https://github.com/projectM-visualizer/projectm), [Homebrew formula](https://formulae.brew.sh/formula/projectm): `LGPL-2.1-or-later`). LGPL allows use from closed-source apps **as a shared library**.
- **Vitality**: very active. Latest releases from the GitHub API: **v4.1.8 (2026-10-06)**, v4.1.7 (2026-07-14), v4.1.6 (2025-11-28), v4.1.5, ... up through v4.0.0 ([releases](https://github.com/projectM-visualizer/projectm/releases)). Cadence is roughly monthly-to-quarterly bugfix releases.
- **Compatibility state** (from the official [Milkdrop Compatibility wiki](https://github.com/projectM-visualizer/projectm/wiki/Milkdrop-Compatibility), reflects master/4.1+):
  - `.milk` parser rewritten for 4.1, now **fully compatible with the MilkDrop 2.2 parser** (defaults/fallbacks match).
  - Equation compiler (projectm-eval) supports loops, regXX, megabuf, documented + undocumented functions/constants, ns-eel2 quirks. Difference: projectM **rejects a whole code block on any syntax error**, whereas MilkDrop silently ignores some errors - so a handful of presets render differently; projectM's stance is "fix the preset".
  - **Shaders**: HLSL is transpiled to GLSL using [unknownworlds/hlslparser](https://github.com/unknownworlds/hlslparser). Known unsupported: (i) **HLSL global/extern shader variables** (cannot be modeled in GLSL - unfixable without manual preset rewrite); (ii) **`SamplerState` structs** (rare; DirectX-only); (iii) assorted DX9-era intrinsics (no complete list).
  - **User sprites**: supported **as of 4.2** (API takes one sprite at a time; blend mode 4 uses alpha instead of DirectX colour-keying).
  - Line rendering is 1px (thick = 2px) regardless of resolution; presets look dim at 4K - same as MilkDrop, "not expected to change" ([issue #682](https://github.com/projectM-visualizer/projectm/issues/682)).
- **Integration surface** (C API, `extern "C"`, `projectM-4/*.h`):
  - Lifecycle: `projectm_create()` / `projectm_create_with_opengl_load_proc(load_proc, user_data)` - **requires a current OpenGL context**; returns NULL otherwise. GL function resolution is global and set on first create ([core.h](https://raw.githubusercontent.com/projectM-visualizer/projectm/master/src/api/include/projectM-4/core.h)).
  - Audio: `projectm_pcm_add_float/int16/uint8(handle, samples, count, channels)` (interleaved LRLR; mono/stereo), `projectm_pcm_get_max_samples()` ([audio.h](https://raw.githubusercontent.com/projectM-visualizer/projectm/master/src/api/include/projectM-4/audio.h)). ProjectM does its own FFT + beat detection.
  - Presets: `projectm_load_preset_file` (path/`file://`/`idle://`) and `projectm_load_preset_data` (Milkdrop text) ([core.h](https://raw.githubusercontent.com/projectM-visualizer/projectm/master/src/api/include/projectM-4/core.h)).
  - Render: `projectm_opengl_render_frame(handle)` and **`projectm_opengl_render_frame_fbo(handle, fbo_id)` (since 4.2.0)**, plus `projectm_opengl_burn_texture(...)` (since 4.2.0) ([render_opengl.h](https://raw.githubusercontent.com/projectM-visualizer/projectm/master/src/api/include/projectM-4/render_opengl.h)). The FBO entry point is the key to offscreen/embedded rendering.
  - Other headers: `callbacks.h`, `parameters.h`, `debug.h`, `logging.h`, `memory.h`, `touch.h`, `user_sprites.h`, `types.h`, `projectM.h`. Legacy `projectm-eval` for expression compilation.
  - Bindings/ports: **Rust** crate `projectm` (`projectm-sys`, [crates.io](https://crates.io/crates/projectm)); frontends SDL (C++), Qt5/Qt6, Rust SDL3, GStreamer plugin. **No maintained C#/.NET binding was found** - `[UNVERIFIED: none exists]`; a thin `DllImport` wrapper over the C API is straightforward.
- Also used by VLC, Kodi, Silverjuke, etc. (third-party integrations, per README).

### 3.2 Butterchurn - MIT, WebGL2, MilkDrop-2 faithful (reference/spec)

- [jberg/butterchurn](https://github.com/jberg/butterchurn): **MIT license**, "a WebGL implementation of the Milkdrop Visualizer". Requires **WebGL 2**.
- **Preset format**: consumes **JSON converted from `.milk`**, not `.milk` directly. Conversion via [jberg/milkdrop-preset-converter](https://github.com/jberg/milkdrop-preset-converter) (and `-node` wrapper); presets distributed as [butterchurn-presets](https://github.com/jberg/butterchurn-presets). This is important: Butterchurn bakes MilkDrop equations/shader text into JSON with a JS expression VM at runtime.
- **Vitality**: npm `butterchurn` latest **2.6.7**, last published **~2019/2020** ([npm](https://www.npmjs.com/package/butterchurn)); repo still referenced widely. Integration: **webamp** (Winamp 2 JS reimplementation), Chrome extension, nightride.fm, mStream, Karaoke Forever, Syqel, etc.
- **Why it matters for us**: Butterchurn (and its converter) is the best **permissively-licensed reference implementation** of MilkDrop-2 semantics in readable JS, and its JSON preset format documents the full parameter/equation set. It cannot be embedded in a .NET/Avalonia GL pipeline directly (it's JS/WebGL2), but it is a strong oracle for differential testing of a projectM-based backend.
- Butterchurn is explicitly "MilkDrop 2"; it does not cover MilkDrop3 `.milk2` or q1-q64.

### 3.3 BeatDrop / MilkDrop3 (Windows, BSD-3)

- MilkDrop3 ([milkdrop2077/MilkDrop3](https://github.com/milkdrop2077/MilkDrop3), site milkdrop3.com, 2023) is **based on BeatDrop** by Maxim Volskiy and adds `.milk2` double-preset blending, any-audio-source capture, q1-q64, larger shape/wave counts, preset editing/mashups ([slskdN design doc](https://github.com/snapetech/slskdN/blob/main/docs/design/webgl-milkdrop3-port.md)).
- Forks: [mvsoft74/BeatDrop](https://github.com/mvsoft74/BeatDrop), [OfficialIncubo/BeatDrop-Music-Visualizer](https://github.com/OfficialIncubo/BeatDrop-Music-Visualizer) ("custom engine for Windows"), [milkdrop2077/BeatDrop2077](https://github.com/milkdrop2077/BeatDrop2077).
- **License**: MilkDrop3 is **BSD-3-Clause** (`code/LICENSE.txt`), but the implementation is **C++/Win32/Direct3D** - a browser or cross-platform port is a rewrite, and the renderer/windowing/input are tied to DirectX/Windows ([slskdN design doc](https://github.com/snapetech/slskdN/blob/main/docs/design/webgl-milkdrop3-port.md)). Useful as a **semantics reference** for MilkDrop3 (q-registers, `.milk2`, sprites), not as an embeddable library.

### 3.4 VSXu - **not a MilkDrop reimplementation, and dead**

- [vovoid/vsxu](https://github.com/vovoid/vsxu): OpenGL visual programming/visualizer, **GPL/LGPL-3.0**, repo **archived 2020-07-06**, last real release 0.6 (2016). It has its own module/preset system; **it does not load MilkDrop presets**. Exclude from consideration.

---

## 4. AVS (Advanced Visualization Studio)

### 4.1 What it is

- Winamp plugin by Justin Frankel et al., first shipped with Winamp 2.61 (v2.0a4); open-sourced **May 2005** under a **BSD-style license**; versions 2.81d/2.82/2.83; a fork "Cockos Happy AVS" 2.9 (2010) ([Wikipedia](https://en.wikipedia.org/wiki/Advanced_Visualization_Studio)).
- **Preset model**: `.avs` files are a **binary preset format** encoding a **tree/stack of components** run top-to-bottom over a shared frame buffer. Categories: **Render / Trans / Misc**, with **Effect Lists** (nested presets). ~**70 effects**, median 4 parameters each, up to 23 ([grandchild AVS Dev Blog #02](https://avs.sh/blog/posts/02-a-cut-right-down-the-middle/)).
- **Scriptable components** use an AVS-specific **EEL** (Nullsoft Expression Evaluation Language; `nseel`, predecessor of ns-eel2) compiled to native code at runtime: SuperScope, Dynamic Movement, Trans, etc. APEs (**AVS Plugin Effects**) are external 32-bit DLLs that extend the effect set - many popular presets depend on them ([Wikipedia](https://en.wikipedia.org/wiki/Advanced_Visualization_Studio)).

### 4.2 Open reimplementations

| Project | Lang / License | Status | Notes |
|---|---|---|---|
| [grandchild/vis_avs](https://github.com/grandchild/vis_avs) | C++ **BSD-3** | Active (50 stars, 19 issues) | Continuation of original source, not a rewrite. Split into `libavs` (+ `avs.h`/`avs_editor.h`) and Winamp frontend; builds `vis_avs.dll` (MSVC/MinGW) **and `libavs.so` on Linux with PipeWire**; new JSON preset format with legacy binary still supported; switched to EEL2. **APEs cannot be loaded**; many are re-integrated as builtin effects. **No 64-bit yet** (roadmap: 64-bit, standalone). "Runs most presets (unless they use rare APEs)." |
| [azeem/webvs](https://github.com/azeem/webvs) | TypeScript **MIT** | Semi-active (93 stars) | Browser AVS rendering library with a built-in JSON preset model; only a subset of effects. Not a full AVS engine. |
| [grandchild/AVS-File-Decoder](https://github.com/grandchild/AVS-File-Decoder/) | TypeScript **MIT** | Maintained | Batch-converts binary **`.avs` -> Webvs JSON**; documents component layout (`doc/components.md`, `saveSections.txt`). Best open documentation of the binary format. |
| [Const-me/vis_avs_dx](https://github.com/Const-me/vis_avs_dx) | C++/DirectX 11 | Stalled | GPU reimplementation, **incomplete**; no APEs. |
| [visbot/vis_avs](https://github.com/visbot/vis_avs) | C++ | Old | Original 2.81d source. |
| [J_Darnley/Advanced-Visualization-Studio](https://gitlab.com/J_Darnley/Advanced-Visualization-Studio) | C | Abandoned | Portability/cleanliness rewrite; never reached compatibility. |
| [vis_avs_tooling](https://github.com/grandchild/vis_avs_tooling) | - | - | Utilities around presets. |

Webvs was historically integrated into browsers around 2013 but **only a subset of effects is supported** ([Wikipedia](https://en.wikipedia.org/wiki/Advanced_Visualization_Studio)).

### 4.3 Honest assessment

**Full AVS compatibility is not practical for a clean-room product.** Reasons:
1. The effect surface is huge (~70 built-ins) plus an out-of-tree **APE DLL** ecosystem that is **32-bit and Windows-only**; the leading continuation (`grandchild/vis_avs`) explicitly **cannot load APEs** and is **32-bit only** today.
2. The preset format is binary, undocumented except by reverse-engineering (`AVS-File-Decoder`), and the EEL dialect is AVS-specific (though EEL2-compatible per grandchild).
3. Every serious rewrite (webvs, vis_avs_dx, Darnley) is partial and stalled.

**Pragmatic options**, in order of effort/ROI:
- **Do MilkDrop via projectM** as the primary target (far larger preset corpus, active upstream, LGPL, cross-platform, GL-based).
- If AVS support is a hard requirement, **embed `libavs`** (BSD-3) as a native library behind the same visualization interface; accept: Linux build available (PipeWire), Windows build 32-bit, no APE loading. Budget for a 64-bit port and an APE-loading story as separate, risky tickets.
- **Do not** attempt a from-scratch AVS reimplementation.

---

## 5. Beat detection

### 5.1 What MilkDrop presets actually consume

MilkDrop presets do not call a beat API directly; they read per-frame audio features exposed to the equation VM ([Authoring Guide](https://raw.githubusercontent.com/afedchin/visualization.milkdrop2/master/visualization.milkdrop2/resources/Milkdrop2/docs/milkdrop_preset_authoring.html)):
- `bass`, `mid`, `treb` - current (read-only) energy of low/mid/high bands, ~1.0 = "normal".
- `bass_att`, `mid_att`, `treb_att` - attenuated (smoothed/damped) versions, which is what most authors use for stable motion.
- `time`, `fps`, `frame`, `progress` plus `meshx/meshy`, `pixelsx/pixelsy`, `aspectx`.

Historically MilkDrop got `spectrumData`/`waveformData` from **Winamp itself** (it is/was a vis plugin), and host-side "on beat" preset advance used Winamp's beat. In projectM, the host only supplies **PCM** (`projectm_pcm_add_*`) and **projectM computes FFT + `bass/mid/treb` and its own beat detector internally** ([audio.h](https://raw.githubusercontent.com/projectM-visualizer/projectm/master/src/api/include/projectM-4/audio.h), [LWN introduction to projectM](https://lwn.net/Articles/750152/)). ProjectM exposes beat sensitivity through its parameters API (`parameters.h`) and there are long-standing tuning discussions ([issue #161](https://github.com/projectM-visualizer/projectm/issues/161), [Steam discussion](https://steamcommunity.com/app/1358800/discussions/0/5413843407455737223/)).

Practical implication: if you use projectM, **you inherit its beat detection** and just feed PCM. You only need your own detector if you (a) display BPM/tempo in the player UI, (b) drive UI animations, or (c) implement a non-projectM backend. `[INFERENCE]` the exact band splits are a weighted log-warped FFT akin to Winamp's `makeSpecData` (2^(x/12) warp over bins), matching the guide's ~1.0-normalized scale.

### 5.2 Cross-platform BPM/tempo libraries

| Library | License | Latest | Language | Notes |
|---|---|---|---|---|
| **aubio** ([aubio.org](https://aubio.org/), [aubio/aubio](https://github.com/aubio/aubio)) | **GPL-3.0** `[UNVERIFIED exact SPDX]` | 0.4.9 (latest tag; long-lived) | C (C API, Python bindings) | Onset/beat/tempo/pitch/MFCC; runs on Linux/Windows/macOS/iOS/Android; C ABI is easy to P/Invoke. Copyleft. |
| **BTrack** ([adamstark/BTrack](https://github.com/adamstark/BTrack)) | **GPL-3.0** | **1.0.7 (2025-12-31)**; 1.0.6 (2025-12-28), 1.0.5 (2023-08-18) | C++ (Python bindings) | Real-time **causal** beat tracker (Davies/Plumbley DP); needs `libsamplerate` + FFTW or Kiss FFT; `processAudioFrame`/`beatDueInCurrentFrame`. Copyleft (GPL) - cannot be linked into a proprietary app. |
| **librosa** ([librosa](https://librosa.org/)) | **ISC** | 0.11.x/1.0 dev | Python/NumPy | `librosa.beat.beat_track` (DP beat tracker + onset). Permissive but Python-only - not embeddable without an interpreter or a C++ rewrite. |
| **Essentia** | AGPL-3.0 | active | C++/Python | Strong rhythm/tempo, but AGPL is the most restrictive option. |
| **Beatroot** | GPL | old | Java | Legacy; no modern cross-platform binding. |
| **SoundTouch** | LGPL-2.1 | active | C++ | Time-stretch/pitch, **not** a beat detector - often mis-cited. |
| **Kiss FFT / FFTW / PFFFT** | BSD-ish / GPL-or-commercial / BSD | active | C | Building blocks if you write your own detector. FFTW is GPL unless you buy the commercial license. |

Licensing warning: **both recommended real-time trackers (aubio, BTrack) are GPL-3.0**. If JellyAmp must be closed-source, either (i) rely solely on projectM's built-in detector, (ii) implement a small own beat/onset detector over a permissive FFT (e.g. PFFFT/Kiss FFT), or (iii) run a GPL tool out-of-process (legal grey area for GPL - verify with counsel). librosa's algorithm (ISC) can be reimplemented freely.

---

## 6. Embedding projectM in Avalonia

### 6.1 What projectM needs

- **A current OpenGL context** created before `projectm_create()` (or a loader callback via `projectm_create_with_opengl_load_proc`). ProjectM supports **OpenGL and OpenGL ES** ([README](https://github.com/projectM-visualizer/projectm), [core.h](https://raw.githubusercontent.com/projectM-visualizer/projectm/master/src/api/include/projectM-4/core.h)).
- Per-frame: add PCM, then `projectm_opengl_render_frame()` (draws into the current context), **or** `projectm_opengl_render_frame_fbo(fbo_id)` (API **4.2.0+**) to render into a caller-owned FBO ([render_opengl.h](https://raw.githubusercontent.com/projectM-visualizer/projectm/master/src/api/include/projectM-4/render_opengl.h)).
- projectM resolves GL functions itself but lets you override via the load-proc callback - important when the GL library is provided by the host (Avalonia/ANGLE/EGL/WGL).

### 6.2 Avalonia rendering options

1. **`OpenGlControlBase` (in-place GL control)** - [docs](https://docs.avaloniaui.net/api/avalonia/opengl/controls/openglcontrolbase), source [`OpenGlControlBase.cs`](https://github.com/AvaloniaUI/Avalonia/blob/main/src/Avalonia.OpenGL/Controls/OpenGlControlBase.cs). Provides `OnOpenGlInit(GlInterface)`, `OnOpenGlDeinit`, `OnOpenGlRender(GlInterface, int fb)`, `RequestNextFrameRendering()`, `InvalidateVisual()`. **Critical caveat from the docs**: *"Any interaction with `GlInterface` should only happen within `OnOpenGlInit/Deinit/Render`... Avalonia makes the context current only during these method calls. Accessing OpenGL functions outside ... may result in undefined behavior, crashes, or rendering corruption."* So a projectM instance must be **created in `OnOpenGlInit`** and **rendered only inside `OnOpenGlRender`**, on the same context. This fits projectM's model (context-bound instance) if you respect the callbacks.
2. **FBO + composition** - create your own GL context (or use the control's), render projectM into an FBO (`projectm_opengl_render_frame_fbo`, 4.2+), then present it in Avalonia either (a) as a **Skia bitmap** via CPU readback (`glReadPixels` -> `Avalonia.Media.Imaging.Bitmap`/`WriteableBitmap`) - simple but slow and copies pixels; or (b) via **`GpuInterop`** ([spec issue #9925](https://github.com/AvaloniaUI/Avalonia/issues/9925)) using an **external GPU image handle** (`ICompositionImportedGpuImage`, `CompositionDrawingSurface.UpdateWithKeyedMutex/UpdateWithSemaphores/UpdateWithAutomaticSync`). This is the zero-copy path but is backend-specific (D3D11/Vulkan handle types).
3. **Native-control embedding** - host projectM in a separate native window/context and composite via WGL/EGL interop. On Windows with Avalonia's default **ANGLE (OpenGL ES over D3D11)** renderer, a native WGL GL texture can be shared with D3D via **`WGL_NV_DX_interop2`** - this is the maintainer's suggested approach for OpenGL<->Avalonia interop ([kekekeks, discussion #19061](https://github.com/AvaloniaUI/Avalonia/discussions/19061)); a user confirmed it works but required forcing the dedicated GPU and correct `wglDXLockObjectsNV`/`wglDXUnlockObjectsNV` locking. This path is Nvidia/ANGLE-specific and fragile.
4. **OpenTK** - [SamboyCoding/OpenTKAvalonia](https://github.com/SamboyCoding/OpenTKAvalonia) exists for OpenTKAvalonia interop; use only if you need OpenTK's GL bindings on top of `OpenGlControlBase`.

### 6.3 Platform pitfalls (must-plan-for items)

- **macOS**: **Avalonia 12 uses Metal by default** on macOS, and `OpenGlControlBase` **does not initialize/render** under Metal - it is inert, not an error ([issue #21133](https://github.com/AvaloniaUI/Avalonia/issues/21133), closed `by-design`, Apr 2026). Workaround: force OpenGL:
  ```csharp
  appBuilder.With(new AvaloniaNativePlatformOptions() {
      RenderingMode = new[] { AvaloniaNativeRenderingMode.OpenGl }
  });
  ```
  A maintainer states Avalonia "will continue supporting `AvaloniaNativeRenderingMode.OpenGl` for a while (at least while Apple supports it)", and **OpenGL interop from the Metal renderer is not planned**. Apple has deprecated OpenGL but still ships it; this is a **medium-term risk** for a GL-only backend on macOS.
- **Windows**: Avalonia defaults to **ANGLE (GLES over D3D11)**; switching to WGL is possible (`Win32RenderingMode.Wgl`) but discouraged ([discussion #6396 referenced in #19061](https://github.com/AvaloniaUI/Avalonia/discussions/19061)). ProjectM supports GLES, so running inside the ANGLE context is plausible; sharing/combining contexts is the hard part.
- **Linux**: Avalonia can use GLX/EGL; projectM works with both. Lowest-risk platform for the in-control approach.
- **Bitness/ABI**: libprojectM `projectm_handle` is an opaque pointer; P/Invoke is trivial (`[DllImport]`/`NativeLibrary`), but the **GL context must be made current by Avalonia** before any projectM call. Export symbols differ per platform (`projectm.dll` / `libprojectM.so` / `libprojectM.dylib`) - use `NativeLibrary.SetDllImportResolver`.
- **No C# binding found**: search returned only Rust (`projectm-sys`), Python, GStreamer, Qt, SDL frontends. Plan to write and maintain a thin P/Invoke layer for the subset in section 3.1.

### 6.4 Recommended integration shape

- Define `IVisualizerEngine` in the player: `LoadPreset(text|path)`, `PushPcm(ReadOnlySpan<float>, channels)`, `Render()`, `Resize(w,h)`; implementations: `ProjectMEngine` (libprojectM), optional `ButterchurnEngine` (out-of-process JS/Electron for parity testing), optional `LibAvsEngine` (libavs), plus native `VisHostEngine` (Windows-32 helper process) if ever needed.
- Render path: prefer a **dedicated `OpenGlControlBase`** for projectM (context-bound instance created in `OnOpenGlInit`, `projectm_opengl_render_frame` in `OnOpenGlRender`) because `projectm_opengl_render_frame_fbo` (4.2+) gives an escape hatch for the FBO/GpuInterop path if the in-control route has trouble on Windows/ANGLE.
- Feed the same PCM ring buffer used by the 10-band EQ and analyser to projectM so visuals and EQ stay in sync.
- Ship the projectM texture pack + a curated preset pack (e.g. Cream of the Crop, or a license-cleared subset); make the preset library folder-configurable for the 130K MegaPack. **Confirm preset/texture redistribution licenses** - preset packs are community-authored; Cream of the Crop is a compilation whose individual preset licenses are not uniformly documented `[UNVERIFIED]`.

---

## 7. Version / license / vitality summary

| Component | Version (as of 2026-10) | License | Vitality |
|---|---|---|---|
| Winamp `vis.h` (Geiss BSD copy) | ABI 1.01 (`0x101`) | BSD-3-Clause (Geiss copy) | frozen format |
| Winamp legacy source (2024 dump) | 5.666-era | contested/restrictive | repo removed from GitHub, mirrors exist |
| libprojectM | **4.1.8 (2026-10-06)**; 4.1.7 (2026-07), 4.1.6 (2025-11) | **LGPL-2.1-or-later** | very active |
| projectm-eval | 1.0.6 | (see repo) | active |
| Butterchurn | 2.6.7 (~2019-2020 npm) | **MIT** | low but stable; JS/WebGL2 only |
| butterchurn-presets / milkdrop-preset-converter | - | MIT | maintained-ish |
| MilkDrop3 / BeatDrop | 3.0 / forks | **BSD-3-Clause** | active (Win32/D3D only) |
| grandchild/vis_avs (`libavs`) | 2.81d+ cleanups | **BSD-3-Clause** | active, no APEs, no 64-bit |
| azeem/webvs | - | MIT | semi-active, partial AVS |
| AVS-File-Decoder | - | MIT | maintained |
| VSXu | 0.6 (2016) | GPL/LGPL-3.0 | **archived 2020** |
| libvisual | 0.4.2 (0.4.x maint.) | LGPL-2.1 `[UNVERIFIED]` | low; 0.5.x unreleased |
| aubio | 0.4.9 | GPL-3.0 | low-moderate |
| BTrack | 1.0.7 (2025-12-31) | GPL-3.0 | active |
| librosa | 0.11.x/1.0dev | ISC | active (Python) |
| Avalonia | 11.3.x stable / 12 preview | MIT | active |

---

## 8. Open questions / to verify before spec freeze

1. **`spectrumData` band count** for the vis.h ABI: Winamp source shows **75 usable values** per channel; the `[576]` is capacity; "288 bands" is `[UNVERIFIED]`. Confirm against the specific Winamp SDK revision you target and against modern WACUP "sa" semantics if vis-plugin support is in scope.
2. **projectM GLES-on-ANGLE** viability on Windows Avalonia (no public integration found) - spike before committing.
3. **macOS OpenGL longevity**: Apple deprecation + Avalonia's Metal-first direction; the forced-OpenGL mode is explicitly "for a while". Consider a Metal-native renderer (or Butterchurn-in-WebView) as a macOS fallback.
4. **Preset/texture redistribution licensing** for bundled packs - audit before shipping the 130K/10K packs.
5. **GPL contamination** of aubio/BTrack if JellyAmp is closed-source; decide own-detector vs projectM-internal detection.
6. **No C# projectM binding exists** - budget for a maintained P/Invoke layer and cross-platform native packaging (NuGet runtime packages for win-x64/win-x86/linux-x64/osx-arm64).

---

## Appendix A - Primary source URLs

- vis.h (Geiss, BSD-3): https://raw.githubusercontent.com/geissomatik/geiss/main/VIS.H
- vis.h mirror (xmp-wavis): https://raw.githubusercontent.com/schellingb/xmp-wavis/master/winamp_vis.h
- xmp-wavis host implementation: https://raw.githubusercontent.com/schellingb/xmp-wavis/master/xmp-wavis.cpp
- Winamp classic_vis.cpp (buffer generation): https://forge.light.kow.is/dkowis/winamp/src/branch/community/Src/Winamp/classic_vis.cpp
- WACUP vis_classic (576 comment): https://github.com/WACUP/vis_classic
- Winamp vis tutorial (archived): https://web.archive.org/web/20181118094338id_/http://www.sulaco.co.za/winamp_tut.htm
- projectM README: https://github.com/projectM-visualizer/projectm
- projectM releases: https://github.com/projectM-visualizer/projectm/releases
- projectM compat wiki: https://github.com/projectM-visualizer/projectm/wiki/Milkdrop-Compatibility
- projectM API headers: https://github.com/projectM-visualizer/projectm/tree/master/src/api/include/projectM-4 (core.h, audio.h, render_opengl.h)
- projectm-eval: https://github.com/projectM-visualizer/projectm-eval
- LWN intro to projectM: https://lwn.net/Articles/750152/
- Butterchurn: https://github.com/jberg/butterchurn
- butterchurn-presets: https://github.com/jberg/butterchurn-presets
- milkdrop-preset-converter: https://github.com/jberg/milkdrop-preset-converter
- MilkDrop Preset Authoring Guide: https://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html
- MilkDrop.org about/authoring: https://milkdrop.org/about , https://milkdrop.org/resources/preset-authoring
- MilkDrop3: https://github.com/milkdrop2077/MilkDrop3 ; BeatDrop: https://github.com/mvsoft74/BeatDrop
- VSXu (archived): https://github.com/vovoid/vsxu
- libvisual: https://libvisual.org/ , https://github.com/Libvisual/libvisual
- AVS Wikipedia: https://en.wikipedia.org/wiki/Advanced_Visualization_Studio
- grandchild/vis_avs: https://github.com/grandchild/vis_avs
- AVS Dev Blog #00/#02: https://avs.sh/blog/posts/00-background/ , https://avs.sh/blog/posts/02-a-cut-right-down-the-middle/
- Webvs: https://github.com/azeem/webvs
- AVS-File-Decoder: https://github.com/grandchild/AVS-File-Decoder/
- Const-me/vis_avs_dx: https://github.com/Const-me/vis_avs_dx
- Avalonia OpenGlControlBase docs: https://docs.avaloniaui.net/api/avalonia/opengl/controls/openglcontrolbase
- Avalonia Metal/OpenGL issue: https://github.com/AvaloniaUI/Avalonia/issues/21133
- Avalonia GPU interop spec: https://github.com/AvaloniaUI/Avalonia/issues/9925
- Avalonia OpenGL GpuInterop Q&A: https://github.com/AvaloniaUI/Avalonia/discussions/19061
- aubio: https://aubio.org/ , https://github.com/aubio/aubio
- BTrack: https://github.com/adamstark/BTrack
- librosa: https://librosa.org/
