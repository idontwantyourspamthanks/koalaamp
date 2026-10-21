# Winamp Skinning System Compatibility — Clean-Room Reimplementation Research

Scope: evidence + URLs for a clean-room, cross-platform (Win/macOS/Linux) Avalonia/.NET Winamp-like player. All version numbers, licenses and vitality are as observed 2026-10-10. Items I could not corroborate are marked **[UNVERIFIED]**. Everything below is web-researched (no repo was modified).

---

## 0. Headline findings

| Question | Answer |
|---|---|
| Does a classic `.wsz` contain a `skin.xml`? | **No.** A `.wsz` is a renamed `.zip` of loose `.bmp` images + a few `.txt` config files. Format is *fixed* (sprite coordinates are baked into Winamp, not the skin). |
| Canonical classic file count | ~**19–45** files (Winamp.com's own tutorial says "a skin is composed of 45 files", counting optional cursors/extras). Not ~100. |
| Is `.wal` a `skin.xml`+PNG container? | **Yes.** `.wal` is also a renamed `.zip`. Root `<WasabiXML version="1.35">` (Winamp 5) or `<WinampAbstractionLayer>` (Winamp3); XML widget tree + PNGs + compiled `.maki`. |
| Do modern skins need Maki? | **Effectively yes.** All mainstream modern skins (Winamp Modern, Bento, MMD3, cPro/Classified) ship `.maki` bytecode for their interactivity. |
| Is a Maki VM/decompiler feasible? | **Yes but expensive.** Bytecode was reverse-engineered (Webamp-Modern, MIT) and the *original* VM source is available (Winamp Collaborative License; qtWasabi ports it). The hard part is the ~65-class stdlib binding surface. |
| Safest OSS references | **MIT**: Webamp, eversolo-winamp, butterchurn. **Copyleft (study only)**: Audacious (BSD-2 core / GPLv3 skins plugin), QMMP (GPL-2+), Xenamp (GPLv2), Galleon (GPLv2). |
| The "famous 999-pixel layout quirk" | **Could not be corroborated [UNVERIFIED]** in the specs, the leaked Winamp draw source, Webamp, or the tutorials. Real layout quirks are the fixed 275-px windows, segment tiling, and sprite-overlap rules (documented below). |
| Legal bottom line | Reimplement the *format* clean-room (Lotus v Borland / Google v Oracle / Sega v Accolade support). Do **not** ship Nullsoft's base-skin artwork; do **not** copy GPL engine code; avoid implying Winamp affiliation. |

---

## 1. Winamp **classic** (WSZ) skin format

### 1.1 Container & file structure
- `.wsz` = a renamed `.zip`; Winamp has read ZIPs directly since the late 2.x era (before that, users extracted to `Winamp\Skins\<name>\`). Any `.zip`/`.wsz` (also `.tgz`/`.tar.gz`/`.tar.bz2` in Audacious) works; extracted folders are also valid. Sources: [Winamp Skin Tutorial – WSZ](https://winampskins.neocities.org/wsz), [Jordan Eldredge – how Winamp2-js loads skins](https://jordaneldredge.com/blog/how-winamp2-js-loads-native-skins-in-your-browser/), [Audacious man page](https://linux.die.net/man/1/audacious).
- Files are matched **case-insensitively by lower-cased basename**, and must survive nesting quirks (`MAIN.BMP`, `Main.bmp`, or inside a folder). Source: [eversolo-winamp/docs/skinning.md](https://raw.githubusercontent.com/quillerpl/eversolo-winamp/main/docs/skinning.md).
- There is **no `skin.xml`** and no per-skin layout data. Winamp hard-codes every sprite's source rectangle. Only the image data and a handful of text parameters are skinnable.

### 1.2 Canonical bitmap set and required sizes
From Fyre's *Winamp Skin Specification v2.0* (reproduced inside the SacRat/Imagine 1.5.0 tutorial PDF) and Strider's spec (both in [WACUP/Winamp-Skinning-Archive](https://github.com/WACUP/Winamp-Skinning-Archive)):

| File | Size (px) | Role |
|---|---|---|
| `main.bmp` | 275 × 116 | Main-window background (the anchor; everything else is composited on it) |
| `titlebar.bmp` | 344 × 87 | Title bars (active/inactive/shade + easter-egg), window buttons, clutterbar, shade seek bar |
| `cbuttons.bmp` | 136 × 36 | Previous/Play/Pause/Stop/Next/Eject, 2 states |
| `shufrep.bmp` | 92 × 85 | Shuffle/Repeat (4 states each, 47-px overlapping) + EQ/PL toggles |
| `posbar.bmp` | 307 × 10 | Seek groove (248 px) + slider (29 px) ×2 states |
| `volume.bmp` | 68 × 433 | 28 volume frames at `y = n*15` + slider at y=422; left half reused as panning |
| `balance.bmp` | 68 × 433 | Same layout as volume; **groove starts at x=9** (first 9 columns are spare — classic off-by-9 trap) |
| `monoster.bmp` | 58 × 33 | Stereo/Mono indicators (sprites overlap by 2 px) |
| `playpaus.bmp` | 42 × 9 | Play/Pause/Stop + work-indicator (overlapping sprites) |
| `numbers.bmp` (or `nums_ex.bmp`) | 99 × 13 | Time digits (10×9 px), minus sign borrowed from digit 1/2 midline |
| `text.bmp` | 155 × 18 | 5×6 bitmap font, grid of 31 cols × 3 rows; uppercase-only set |
| `pledit.bmp` | 280 × 186 | Playlist editor (title/border tiles + ~30 buttons) |
| `eqmain.bmp` | 275 × 315 | Equalizer window (titlebar, 11 slider grooves, 61 bars each, ON/AUTO/preset) |
| `eq_ex.bmp` | 275 × 54 | Equalizer windowshade |
| `mb.bmp` | ~205 × 160 | Mini-browser (window tiles + nav buttons) |
| `avs.bmp` | ~97 × 190 | Advanced Visualization Studio window chrome |
| `video.bmp` | variable | Video window (resizable/tiled) |
| `gen.bmp` | variable | "General purpose" windows (Media Library, AVS, etc.) — tiled titlebar/borders + its own variable-width font |
| `genex.bmp` | variable | Buttons, scrollbars and the **18 colour-pixel swatches** for gen windows (`y=0`, `x=48..82`; the 130x75 reference atlas) |

Optional cursors (loaded by basename, `.cur`/`.ani`): `MAINMENU, MIN, NORMAL, PCLOSE, PNORMAL, POSBAR, PSIZE, PTBAR, PVSCROLL, PWINBUT, PWSNORM, PWSSIZE, SONGNAME, TITLEBAR, VOLBAL, WINBUT, WSNORMAL, WSPOSBAR, EQCLOSE, EQNORMAL, EQSLID, EQTITLE, CLOSE, MMENU` — four further cursors (`VOLBAR`, `WSCLOSE`, `WSWINBUT`, `WSMIN`) exist but are unused. Source: [Webamp skinParser.js CURSORS list](https://raw.githubusercontent.com/captbaritone/webamp/master/packages/webamp/js/skinParser.js), quoting the Skinners Atlas.

BMP constraints (Strider spec): 1/4/8/24-bit, RLE or uncompressed are accepted; oversized images are cropped, undersized leave holes; **1-bit `main.bmp` forces a B/W UI**. Practical gotcha: many real skins store `MAIN/CBUTTONS/TEXT/NUMBERS/SHUFREP/POSBAR/MONOSTER` as **BI_RLE8**, which Android's `BitmapFactory` (and some naïve decoders) cannot read — see [eversolo skinning.md](https://raw.githubusercontent.com/quillerpl/eversolo-winamp/main/docs/skinning.md). A robust decoder must handle BI_RLE8 + 4/8/24/32-bit.

### 1.3 Configuration / parameter files
- **`viscolor.txt`** — 24 lines of `R,G,B`: line 0 = vis background, 1 = background dots, 2–17 = spectrum gradient (2 top … 17 bottom), 18–22 = oscilloscope (18 brightest … 22 dimmest), 23 = peak marker. Sources: [Strider spec](https://github.com/WACUP/Winamp-Skinning-Archive/blob/master/Classic%20Skins/skinspecs.pdf), [Winamp Skin Tutorial – Config](https://winampskins.neocities.org/config), [Fyre spec §viscolor].
- **`pledit.txt`** — `[Text]` section with `Normal`, `Current`, `NormalBG`, `SelectedBG`, `MbFG`, `MbBG`, `Font` (hex `#RRGGBB`; `Font=` names a system font for playlist/minibrowser). Same sources.
- **`region.txt`** — window-shape/transparency definition. Four sections: `[Normal]` (main), `[WindowShade]`, `[Equalizer]`, `[EqualizerWS]`. Each has `NumPoints=` (comma list: number of vertices per polygon) and `PointList=` (comma-separated x,y pairs). Points define the **visible** area (not the cut-out); mismatched counts crash Winamp. Webamp's parser drops polygons with <3 points and tolerates space/comma separators: [regionParser.ts](https://raw.githubusercontent.com/captbaritone/webamp/master/packages/webamp/js/regionParser.ts). Tutorial: [Winamp Skin Tutorial – Config](https://winampskins.neocities.org/config).
- Other optional text files: `readme.txt`, `winampmb.txt`/`mb.ini` (minibrowser home URL), plus skin-supplied plugin files.

### 1.4 Region/transparency and compositing quirks (the *real* "layout quirks")
- Non-rectangular skins use `region.txt`; per-pixel alpha is impossible in classic skins (the UI is built by layering opaque sprites) — [Winamp Skin Tutorial – Main](https://winampskins.neocities.org/main).
- **Fixed window widths**: main/EQ are 275 px; only playlist/video/gen windows resize, and they resize by **tiling** edges in fixed segments. Webamp models this with `WINDOW_RESIZE_SEGMENT_WIDTH = 25`, `WINDOW_RESIZE_SEGMENT_HEIGHT = 29`, `WINDOW_WIDTH = 275`, `WINDOW_HEIGHT = 116`, `TRACK_HEIGHT = 13` ([constants.ts](https://raw.githubusercontent.com/captbaritone/webamp/master/packages/webamp/js/constants.ts)).
- **Sprite overlaps baked into Winamp**: shuffle/repeat overlap by 1 px (last-pressed wins); mono overlaps stereo by 2 px; play indicator overlaps the work indicator; active windowshade bottom row == inactive windowshade top row; playlist title bar tiles are 25 px with only two of six columns seamless. Sources: Strider spec; Fyre "BUGZ" section; [eversolo skinning.md](https://raw.githubusercontent.com/quillerpl/eversolo-winamp/main/docs/skinning.md).
- `gen.bmp` marks its own tileable pieces in **RGB(0,198,255)** — a colour used nowhere else (eversolo measured this). `genex.bmp` carries the gen-window colour table: **18** swatch pixels at `y=0`, `x=48,50,52,…82` (item background/foreground, window/button text, divider, selection, six header colours, five scrollbar colours). The archived tutorial lists these 18 columns and the four corpus `genex.bmp` sheets (all 130x75) confirm them; the "24" figure in older notes belongs to `viscolor.txt`, not genex ([Other Windows tutorial](https://winampskins.neocities.org/twonine), [For Winamp 2.9/5.x (archived wiki)](https://web.archive.org/web/20240118132635/http://wiki.winamp.com/wiki/For_Winamp_2.9/5.x)). The same atlas supplies 47x15 active/pressed buttons, 14x14 scroll arrows and fixed-length 14x28 / 28x14 slider strips.
- **The "999-pixel" claim: [UNVERIFIED].** I searched Strider's v1.2.1 spec, Fyre's v2.0 spec, the SacRat/Imagine tutorial, the Skinners Atlas scrape, Webamp's `skinSprites.ts`/`skinParser`/`constants`, and the leaked Winamp draw source (`draw_main.cpp`, `draw_eq.cpp`, `draw_mb.cpp`, `draw_sa.cpp`, `draw_pe.cpp`, `SkinUtils.cpp`, `Skins.cpp`, `main_init.cpp`, `Pledit.cpp`) — **no 999-px layout rule exists**. If this claim came from a specific ticket source, the author should re-verify; the closest verified facts are the fixed 275-px windows, 25-px tiling, and the 0..~344/0..433 sprite coordinate ranges.

### 1.5 Authoritative references for classic skins
- Strider, **Unofficial WinAMP Skin Specifications v1.2.1** (Apr 1998) — PDF: https://github.com/WACUP/Winamp-Skinning-Archive/blob/master/Classic%20Skins/skinspecs.pdf
- **Fyre, Winamp Skin Specification v2.0** (bundled in) and SacRat & Imagine, **Winamp Skinning Tutorial v1.5.0** — PDF: https://github.com/WACUP/Winamp-Skinning-Archive/blob/master/Classic%20Skins/Winamp_skinning_tutorial_1_5_0.pdf
- Jellby, **Skinners Atlas v1.5.1** — https://github.com/WACUP/Winamp-Skinning-Archive/tree/master/Classic%20Skins/Skinners%20Atlas
- Archived Winamp Developer Wiki pages: [Creating Classic Skins](http://wiki.winamp.com/wiki/Creating_Classic_Skins) (DNS now dead; use https://web.archive.org/web/2024/http://wiki.winamp.com/wiki/…), [For Winamp 2.9/5.x](https://web.archive.org/web/20240118132635/http://wiki.winamp.com/wiki/For_Winamp_2.9/5.x), [Editing the Configuration Files], [WSZ Files].
- Modern-era community mirror: [winampskins.neocities.org](https://winampskins.neocities.org/config) (archived official tutorial), [Swiftys HQ](https://swiftyshq.neocities.org/random/wampc/).
- Code-level canonical sprite table: [Webamp `skinSprites.ts`](https://raw.githubusercontent.com/captbaritone/webamp/master/packages/webamp/js/skinSprites.ts) (MIT).
- Historical archive also at [archive.org skinspecs](https://raw.githubusercontent.com/WACUP/Winamp-Skinning-Archive/master/Classic%20Skins/skinspecs.pdf) and [winampheritage.com](https://winampheritage.com/version-history).

---

## 2. Winamp **modern / modern-classic** skins (`.wal`) and Maki

### 2.1 Container and XML structure
- `.wal` is a renamed `.zip` containing XML, PNG images, fonts and `.maki` scripts ([Jordan Eldredge – "Rendering modern Winamp skins in the browser", 2024](https://jordaneldredge.com/webamp-modern/)).
- Root element is `<WasabiXML version="1.35">` for Winamp 5 skins; Winamp3 used `<WinampAbstractionLayer version="…">`. Example (Winamp5 base skin): https://raw.githubusercontent.com/0x5066/WinampModernPP/WACUPchanges/skin.xml — contains `<skininfo>`, `<accelerators>`, a set of `<include file="xml/…"/>`, and `<scripts><script file="scripts\wa2songtimer.maki"/></scripts>`.
- Typical layout (Winamp Modern default skin shipped in Winamp source): `skin.xml`, `about/`, `notifier/`, `player/`, `pledit/` (or `xml/pledit*.xml`), `ml/`, `vis/`, `video/`, `albumart/`, `shade/`, `standardframe/`, `titlebar/`, `window/`, `xml/` (player.xml, player-normal.xml, player-shade.xml, playlistpro.xml, color-presets.xml, system-colors.xml, …), `scripts/*.maki`, plus image folders. Verified from the Winamp source mirror: https://forge.light.kow.is/dkowis/winamp/src/branch/community/Src/resources/skins/Winamp%20Modern/xml
- **Widget/container model** (Wasabi): `layout` → `container` → `group`/`groupdef` → `layer`/`button`/`slider`/`text`/`animatedlayer`/`vis`/`timer`/`customobject`; `@embed`, `x/y/w/h`, `relatx/relaty`, `gammaset`/`colorpresets`, `snap` points, `drawer` containers, `windowholder` for embedded components. Generic windows (Media Library, Playlist, Video, AVS) are hosted in `<windowholder>` bound to component GUIDs — the "GenericWnd"/gen concept; classic skins approximate this with `gen.bmp`/`genex.bmp`. Docs (archived): [XML GUI Objects](https://web.archive.org/web/2024/http://wiki.winamp.com/wiki/XML_GUI_Objects), [Modern Skin: Container](https://web.archive.org/web/20240711113516/http://wiki.winamp.com/wiki/Modern_Skin:_Maki_Overview), [Modern Skin: Maki Overview].
- Modern art is **PNG** (with alpha), unlike classic BMPs — giving per-pixel transparency, animations, gamma/colour themes, and custom layouts.

### 2.2 Maki — the make-or-break component
- Maki ("**M**ake **A** **K**iller **I**nterface") is a small object-oriented language compiled by `MC.EXE` to a **custom bytecode** (`.maki`). It handles events (`onClick`, `onTimer`, `System.onScriptLoaded`), UI manipulation, playback/EQ control, timers, etc. Skinners had to ship compiled `.maki`; only some also shipped `.m` sources. Source: [Modern Skin: Maki Overview (archived)](https://web.archive.org/web/20240711113516/http://wiki.winamp.com/wiki/Modern_Skin:_Maki_Overview).
- The bytecode format was undocumented but has been **fully reverse-engineered and documented** by Jordan Eldredge: 7 mandatory sections (Header magic `"FG"`; Types/GUIDs; Methods; Variables; Strings; Bindings; Code) + 2 optional debug sections; a documented opcode table (`call`=0x18, `call global`=0x19, `mov`=0x30, `add`=0x40, … `strange call`=0x70); stack-protection word added in Winamp 5.02/5.03. Doc: [`maki-bytecode.md`](https://github.com/captbaritone/webamp/blob/master/packages/webamp-modern/src/maki/maki-bytecode.md).
- An older **decompiler** exists (Ralf Engels' Perl `maki_decompiler`, archived: https://web.archive.org/web/20180627114343/http://www.rengels.de/maki_decompiler/doc.html) — it produced Maki source from bytecode and was the basis for Eldredge's parser.
- **Feasibility assessment**: a bytecode interpreter is well within reach (Eldredge shipped one in TypeScript). The **expensive part is the stdlib/`std.mi` surface** — roughly **65 native classes** each with many methods/properties (`System`, `Container`, `Group`, `Layer`, `Button`, `Slider`, `Text`, `Timer`, `AnimatedLayer`, `GuiObject`, `Map`, `List`, `Config`, …) that must be bridged to your UI toolkit ([Eldredge blog](https://jordaneldredge.com/webamp-modern/); stdlib: https://github.com/captbaritone/webamp/tree/master/packages/webamp-modern/resources/maki_compiler). Webamp-Modern is a *partial* implementation; qtWasabi's own fidelity audit reports only **1 of 5** showcase skins pixel-faithful as of the repo state, with Maki VM parity as an ongoing workstream ([qtamp.org](https://qtamp.org/), [qtWasabi README](https://github.com/qtWasabi/qtWasabi)).
- Two viable engine strategies:
  1. **Clean-room VM** — reimplement from `maki-bytecode.md` (MIT), write your own ~65 class bindings. Highest legal cleanliness, highest engineering cost.
  2. **Port the real VM** — qtWasabi compiles the *original opened* Maki VM (`/Src/Wasabi/api/script/`) unmodified and shims the ~40 native classes to Qt widgets. This is bit-exact on obscure quirks but the VM source is **Winamp Collaborative License v1.0** (user-supplied at build time, not redistributable). qtWasabi's own code is MIT.

### 2.3 Most-used modern skins and their Maki dependence
- **Winamp Modern** (default since 5.0), **Bento** (default since 5.5), **MMD3**, **cPro / Classified**, **WinampModernPP** (MIT fork by 0x5066), **CornerAmp_Redux**. All are XML+PNG+**Maki**; MMD3 is notably script-heavy (Webamp-Modern's TODO cites MMD3 scrolling/volume bugs). Even the "base" Winamp5 Modern skin ships `scripts\wa2songtimer.maki` (see its `skin.xml`). Conclusion: **you cannot credibly claim modern-skin compatibility without a working Maki VM.**
- Winamp3 docs (referred to as "modern / modern-classic") also use this engine; Winamp 5's "modern-classic" is the same `.wal` format rendered by the Wasabi/`gen_ff` plugin.

---

## 3. Existing open-source implementations (licenses & clean-room usability)

| Project | What it is | License | Vitality | Clean-room usable? |
|---|---|---|---|---|
| **Webamp** ([captbaritone/webamp](https://github.com/captbaritone/webamp)) | Classic-skin player in TS; pixel-perfect Winamp 2.9 rendering; `skinSprites.ts` = canonical sprite table; exports `ani-cursor`, `winamp-eqf`, `webamp-modern` | **MIT** | 11.2k★, actively maintained (2026) | **Yes** — read/adapt with attribution. Best source for classic coordinates + region parsing. |
| **webamp-modern** (in same monorepo) | Partial browser renderer for `.wal` + Maki VM + bytecode docs | **MIT** | Active-ish, incomplete (stdlib coverage) | **Yes** — but incomplete; great for the Maki format spec. |
| **eversolo-winamp** ([quillerpl/eversolo-winamp](https://github.com/quillerpl/eversolo-winamp)) | Recent (2025) Android/JVM classic player; docs on RLE8, generated sprite tables, quirks | **MIT** | New (2025), 1★ | **Yes** — excellent engineering notes on real-world skin traps. |
| **QMMP** ([qmmp.ylsoftware.com](https://qmmp.ylsoftware.com/)) | Qt player with a `Ui/skinned` plugin supporting Winamp classic `.wsz` | **GPL-2+** (some files LGPL/BSD/CC-BY-SA) | v2.2.4 (2025-03) | **Study only** — copying code forces GPLv2+. |
| **Audacious** ([audacious-media-player](https://github.com/audacious-media-player/audacious)) | Core BSD-2; "Winamp Classic Interface" plugin (Qt) renders Winamp 2 skins | core **BSD-2**; `skins-qt` plugin **GPLv3** | v4.4.1 (2025) | Plugin code is GPLv3 — study only. Note: **classic skins are NOT dropped**; the plugin is Qt-only and warns it needs Xwayland on Wayland. |
| **qtWasabi / qtamp** ([qtWasabi](https://github.com/qtWasabi/qtWasabi), [qtamp.org](https://qtamp.org/)) | Faithful Wasabi/Maki engine + reference player for modern `.wal` | **MIT** wrapper; ports user-supplied **WCL v1.0** VM source | Active (2025); Windows port "someday" | **Split model**: study MIT code freely; VM source is per-build user-supplied under WCL. |
| **Galleon `ClassicSkin.java`** ([tivo-galleon](https://github.com/jradwan/tivo-galleon)) | Java classic-skin renderer (modified JavaZoom), hard-coded sprite panels | **GPLv2** | Dormant (2005) | Study only. Useful for a second sprite-coordinate table. |
| **Xenamp / WinampSkin** ([djshaji/WinampSkin](https://github.com/djshaji/WinampSkin) → [djshaji/eva](https://github.com/djshaji/eva)) | Android Java classic-skin engine, ~65k skins | **GPLv2** | Alpha/dormant | Study only. |
| **aamp** | *No Winamp skin project exists.* `github.com/aday2deep/aamp` → **404**; searches surface only RDK's unrelated streaming "AAMP". | — | — | N/A — recommend dropping this lead. |
| **Butterchurn** ([jberg/butterchurn](https://github.com/jberg/butterchurn)) | WebGL MilkDrop visualizer | **MIT** | 2.0k★ | Bottom half of the MilkDrop story (visuals). |
| **projectM** ([projectM-visualizer/projectm](https://github.com/projectM-visualizer/projectm)) | Cross-platform MilkDrop-compatible viz library | **LGPL-2.1** | 4.5k★, libprojectM 4.1 | Usable as LGPL (dynamic link) — good for desktop MilkDrop. |
| **AVS source** ([1014.org/code/nullsoft/avs](https://www.1014.org/code/nullsoft/avs/)) | Nullsoft Advanced Visualization Studio | **BSD-3-clause-style (2005 Nullsoft)** | Frozen 2005 | **Yes** — permissive; reimplementable. |

**Clean-room guidance:**
- MIT sources (**Webamp, eversolo, butterchurn, qtamp/qtWasabi engine**) are compatible with reading/adapting into a permissive/closed project with attribution.
- **GPL/BSD-copyleft** sources (QMMP, Audacious plugins, Xenamp, Galleon) may be *read* for learning, but copying any code makes your derived work copyleft. Prefer black-box/behavioral reference.
- **Do not** copy the Winamp/Wasabi source. The 2024 release used the **Winamp Collaborative License v1.0/1.0.1**, which forbids distributing modified versions and (v1.0) forking; §3 only lets you run/propagate covered works you don't convey. qtWasabi's user-supplied-at-build pattern is the standard workaround but still adds friction.
- Webamp's README is explicit that while the *code* is MIT, the Winamp name/interface/sample audio are Nullsoft property — keep your branding distinct.

---

## 4. Skin archives / corpora for testing

- **Internet Archive — "Winamp Skins Collection"**: https://archive.org/details/winampskins (and per-skin items `archive.org/details/winampskins_<name>`). Grew from ~64k skins at the 2020 museum launch → ~65k (2021 press) → **100,000+ on 2024-07-30** ([Jordan Eldredge – "100k skins"](https://jordaneldredge.com/skin-upload/); early coverage: [The Verge](https://www.theverge.com/tldr/21430347/winamp-skin-museum-nostalgia-90s-00s-internet-art-history), [PC Gamer](https://www.pcgamer.com/heres-an-interactive-archive-of-65000-winamp-skins-for-you-to-browse-forever/)).
- **Winamp Skin Museum** — https://skins.webamp.org/ (about: https://skins.webamp.org/about). It is a fast searchable front-end over the archive collection; it stores metadata in an SQLite DB, and its **upload flow** (drag whole directories, client-side MD5 dedupe, direct-to-S3/R2 upload) is the practical way to contribute/get bulk data ([design write-up](https://jordaneldredge.com/skin-upload/)).
- **Recommended test skins**: the official base `base-2.91.wsz` (museum: https://skins.webamp.org/skin/5e4f10275dcb1fb211d4a8b4f1bda236/base-2.91.wsz/), the standard `Classic.wsz`, and a corpus including RLE8-heavy, region.txt-heavy, and cursor-bearing skins.
- **Regression corpus precedents**: Webamp ships fixtures (`ani-cursor` `.cur`/`.ani`, regionParser tests); qtWasabi maintains a curated pixel-regression corpus (currently 5 skins); eversolo generated sprite tables from Webamp (and warns eva's `skinformat.json` contradicts itself). For your project, snapshot-test against the museum's highest-downloaded skins plus deliberately adversarial ones (the museum also documents corrupted/redacted skins: https://jordaneldredge.com/corrupted-skins/).
- **Sizes/licensing**: individual skins are typically 50–300 KB; **each skin's artwork remains © its author** — the archive is a preservation index, not a relicense. Bundle/test locally; don't redistribute Nullsoft's base skin.

---

## 5. Legal analysis

### 5.1 Ownership timeline
- Nullsoft (Justin Frankel & Dmitry Boldyrev) → **AOL, 1999 ($80M)** → AOL/Time-Warner → **Radionomy, Jan 2014** ($5–10M, AOL took 12% stake) → **Vivendi majority stake, Dec 2015** → Radionomy rebranded **Llama Group** → officially renamed **Winamp Group SA** (Oct 2025; Euronext ticker ALWIN). Latest legacy player: **Winamp 5.9.2 (2023-04-26)**; a separate "Winamp for Creators" streaming platform launched Sept 2024. Sources: [Winamp – Wikipedia](https://en.wikipedia.org/wiki/Winamp), [Variety 2015](https://variety.com/2015/digital/news/vivendi-buys-majority-stake-in-winamp-shoutcast-owner-radionomy-1201664170), [Business Wire – Llama Group becomes Winamp Group SA (2025-10-20)](https://www.businesswire.com/news/home/20251020592923/en/Llama-Group-Officially-Becomes-Winamp-Group-SA), [winamp.com/player/legacy](https://winamp.com/player/legacy).

### 5.2 The 2024 "open source" release and its license
- Llama Group released partial Winamp source on 2024-09-24 under the **Winamp Collaborative License (WCL) v1.0** (shortly revised to v1.0.1). It is **not** free/open source: it forbids distributing modified versions and forking, requires assignment of contribution rights, and is governed by Belgian law. Full text (archived mirror): https://git.axenov.dev/Museum/winamp/raw/commit/839d68c00c6a3e49da8f8759e738d6adc8e9f677/LICENSE.md
- The release also contained code the company likely had no right to publish (SHOUTcast DNAS server code it had sold to Azerion in 2022, plus Microsoft/Intel codecs and GPLv2 code), and the **entire repository was deleted on 2024-10-16** after *The Register* enquired. Sources: [The Register](https://www.theregister.com/software/2024/10/16/releasing-winamp-source-goes-badly-for-its-owners-anyway/417709), [Ars Technica](https://arstechnica.com/gadgets/2024/10/winamp-really-whips-open-source-coders-into-frenzy-with-its-source-release/), [The Verge](https://www.theverge.com/2024/9/25/24254341/winamps-source-code-is-online-but-you-cant-make-anything-with-it), [FSF statement](https://www.fsf.org/blogs/community/winamp-failed-to-confuse-people-about-software-freedom); mirrors survive ([Kowis forge](https://forge.light.kow.is/dkowis/winamp), [Axenov Museum](https://git.axenov.dev/Museum/winamp)).

### 5.3 Terms of use / EULA and known enforcement
- Current winamp.com "legal" pages are thin: a general Terms-of-Use shell and "Winamp for Creators" general terms (mostly marketing boilerplate) — [winamp.com/legal-creators/general-terms-of-use](https://winamp.com/legal-creators/general-terms-of-use). The legacy-player page ties contributions to a **non-commercial** contract and 2 official releases/year — [winamp.com/player/legacy](https://winamp.com/player/legacy).
- Historic Winamp 2.x was distributed as freeware/shareware (Strider spec; Winamp source `README.TXT`); Nullsoft ran a **skin submission agreement** for winamp.com uploads (archived wiki: [Submitting Your Skin to Winamp.com](https://web.archive.org/web/2024/http://wiki.winamp.com/wiki/Submitting_Your_Skin_to_Winamp.com)). Nullsoft's only famous C&D was PlayMedia/AMP over the MP3 decoder (1998–99) — **not about skins** ([Winamp – Wikipedia](https://en.wikipedia.org/wiki/Winamp)). I found **no evidence of a skin-format-specific C&D** [UNVERIFIED — none found].
- Trademarks: "Winamp", "Nullsoft", "SHOUTcast", "Llama" remain Winamp Group property; individual skin artwork remains its author's copyright. Nominative use ("re-implements the Winamp 2 skin format") is generally defensible in the US, but avoid logos/mascot/implying endorsement, especially for EU distribution.

### 5.4 Is the skin *format* protectable?
The WSZ/WAL layout — file names, fixed sprite rectangles, `region.txt`/`viscolor.txt`/`pledit.txt` semantics — is a de-facto **interoperability interface**, not creative expression. Supporting law:
- **Lotus Development v. Borland** (1st Cir. 1995): menu command hierarchy held an unprotectable "method of operation"; aff'd by an equally divided Supreme Court (no national precedent) — https://cyber.harvard.edu/people/tfisher/IP/1995_Lotus.pdf
- **Google v. Oracle** (SCOTUS 2021): reimplementation of Java API declarations was **fair use** (a fair-use, not copyrightability, holding) — https://www.supremecourt.gov/opinions/20pdf/18-956_new_0e04.pdf ; commentary: [Texas Law Review – Interfaces and Interoperability After Google v. Oracle](https://texaslawreview.org/interfaces-and-interoperability-after-google-v-oracle/).
- **Sega v. Accolade / Sony v. Connectix** (9th Cir.): reverse engineering for interoperability is fair use.

### 5.5 Practical recommendations for JellyAmp
1. Implement the classic and modern formats **clean-room from public specs + observed behaviour**; use MIT references (Webamp, eversolo, webamp-modern) for coordinates and parser edge cases.
2. **Never ship the Nullsoft base skin** or bundled proprietary skins; the app should ask the user for one (the eversolo pattern).
3. If you need modern-skin support, plan for a Maki VM: either a clean-room VM (MIT docs) or, if you accept build-time user-supplied WCL source (qtWasabi pattern). Do not redistribute WCL-derived binaries.
4. Keep branding distinct from "Winamp"/Nullsoft; use nominative “supports Winamp skins” phrasing only.
5. Track the museum corpus but don't redistribute it.

---

## 6. Open items / explicitly unverified
- **"999-pixel layout quirk"** — not found in any primary source (see §1.4). **[UNVERIFIED]**
- **"aamp"** — `github.com/aday2deep/aamp` is 404; no Winamp-skin project named aamp found. **[UNVERIFIED / lead likely wrong]**
- Modern-skin **popularity ranking** is qualitative (default skins + MMD3/cPro are the well-known ones); no download statistics were retrievable.
- Audacious/GTK vs Qt skin support nuance: the Winamp-skin plugin is Qt-only today and needs Xwayland on Wayland; exact GTK history not fully traced.
- Download counts / last-release dates for some minor tools were not all obtained.

---

## 7. Source index (all load-bearing URLs)
- WACUP Skin Archive (Strider spec, tutorial 1.5.0, Skinners Atlas): https://github.com/WACUP/Winamp-Skinning-Archive
- Winamp Skin Tutorial (official tutorial mirror): https://winampskins.neocities.org/config , /main , /wsz
- Fyre spec v2.0 (inside tutorial PDF): https://raw.githubusercontent.com/WACUP/Winamp-Skinning-Archive/master/Classic%20Skins/Winamp_skinning_tutorial_1_5_0.pdf
- Webamp (MIT): https://github.com/captbaritone/webamp ; sprites https://raw.githubusercontent.com/captbaritone/webamp/master/packages/webamp/js/skinSprites.ts ; parser https://raw.githubusercontent.com/captbaritone/webamp/master/packages/webamp/js/skinParser.js ; region https://raw.githubusercontent.com/captbaritone/webamp/master/packages/webamp/js/regionParser.ts ; constants https://raw.githubusercontent.com/captbaritone/webamp/master/packages/webamp/js/constants.ts
- Maki bytecode doc: https://github.com/captbaritone/webamp/blob/master/packages/webamp-modern/src/maki/maki-bytecode.md
- Webamp-Modern write-up: https://jordaneldredge.com/webamp-modern/
- Winamp source mirror: https://forge.light.kow.is/dkowis/winamp
- Winamp Modern skin XML: https://forge.light.kow.is/dkowis/winamp/src/branch/community/Src/resources/skins/Winamp%20Modern/xml ; https://github.com/0x5066/WinampModernPP
- qtWasabi: https://github.com/qtWasabi/qtWasabi ; qtamp: https://qtamp.org/
- Audacious: https://github.com/audacious-media-player/audacious-plugins/blob/master/src/skins-qt/plugin.cc ; README https://raw.githubusercontent.com/audacious-media-player/audacious/master/README.md
- QMMP: https://qmmp.ylsoftware.com/ ; Debian copyright https://metadata.ftp-master.debian.org/changelogs/main/q/qmmp/qmmp_2.2.4-1_copyright
- eversolo: https://github.com/quillerpl/eversolo-winamp ; https://raw.githubusercontent.com/quillerpl/eversolo-winamp/main/docs/skinning.md
- Xenamp: https://github.com/djshaji/WinampSkin ; Galleon: https://raw.githubusercontent.com/jradwan/tivo-galleon/master/galleon/src/org/lnicholls/galleon/winamp/ClassicSkin.java
- Vis: butterchurn https://github.com/jberg/butterchurn (MIT) ; projectM https://github.com/projectM-visualizer/projectm (LGPL-2.1) ; AVS https://www.1014.org/code/nullsoft/avs/
- Archive/Museum: https://archive.org/details/winampskins ; https://skins.webamp.org/ ; https://jordaneldredge.com/skin-upload/ ; https://skins.webamp.org/skin/5e4f10275dcb1fb211d4a8b4f1bda236/base-2.91.wsz/
- Legal: https://en.wikipedia.org/wiki/Winamp ; WCL https://git.axenov.dev/Museum/winamp/raw/commit/839d68c00c6a3e49da8f8759e738d6adc8e9f677/LICENSE.md ; https://www.theregister.com/software/2024/10/16/releasing-winamp-source-goes-badly-for-its-owners-anyway/417709 ; https://www.fsf.org/blogs/community/winamp-failed-to-confuse-people-about-software-freedom ; Lotus https://cyber.harvard.edu/people/tfisher/IP/1995_Lotus.pdf ; Oracle https://www.supremecourt.gov/opinions/20pdf/18-956_new_0e04.pdf ; https://texaslawreview.org/interfaces-and-interoperability-after-google-v-oracle/ ; https://winamp.com/player/legacy ; https://www.businesswire.com/news/home/20251020592923/en/Llama-Group-Officially-Becomes-Winamp-Group-SA
