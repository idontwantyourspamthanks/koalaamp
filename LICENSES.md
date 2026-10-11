# Third-party licenses

KoalaAmp is MIT-licensed (see the repository). This file lists every
third-party dependency, its license, its link mode, its pinned version, and its
source. It is **human-maintained** from now on; the machine-readable twin is
[`deps/dependencies.json`](deps/dependencies.json), which is enforced by
[`scripts/license-audit.sh`](scripts/license-audit.sh) (LICENSE auditing: no
GPL/AGPL; LGPL components must be dynamically linked).

**Discipline** (see `docs/feasibility.md` §6): every LGPL component ships
**dynamically linked** as separate `.so`/`.dylib`/`.dll` — never statically
linked into the application binary and never single-file-merged. No GPL/AGPL
component is used. Clean-room MIT references (Webamp, projectm-eval) are read
and adapted with attribution in the file header, not linked.

## Runtime dependencies

| Dependency | Version | License | Link mode | Source |
|---|---|---|---|---|
| Qt | 6.12.0 (CI Linux/macOS; Core/Gui/Widgets/OpenGL) · 6.10.3 (CI Windows) · 6.10.2 (local dev) | LGPL-3.0-only | dynamic | https://code.qt.io/cgit/qt/ |
| libvlc | 3.0.x | LGPL-2.1-or-later | dynamic | https://code.videolan.org/videolan/vlc |
| TagLib | 2.3.2 | LGPL-2.1-or-later OR MPL-2.0 | dynamic | https://github.com/taglib/taglib |
| projectM | 4.2.0-dev (master @ 349b7cc2; 4.1.8 lacks the FBO API QOpenGLWidget needs) | LGPL-2.1-or-later | dynamic | https://github.com/projectM-visualizer/projectm |
| SQLite | 3.46.x (FTS5) | public domain | static (permitted) | https://sqlite.org/ |
| miniaudio | 0.11.22 | MIT-0 (dual public domain) | header-only | https://github.com/mackron/miniaudio |
| libcurl | 8.18.0 | curl | dynamic | https://curl.se/libcurl/ |
| zlib | system (Linux/macOS) / vcpkg (Windows) | zlib | dynamic | https://zlib.net/ |
| JSON | 3.11.3 (nlohmann/json) | MIT | header-only | https://github.com/nlohmann/json |
## Build / test dependencies (not shipped)

| Dependency | Version | License | Link mode | Source |
|---|---|---|---|---|
| Catch2 | 3.7.1 | BSL-1.0 | header/source, build-time only | https://github.com/catchorg/Catch2 |

## Notes

- **Clean-room MIT references (read/adapted, *not* linked):**
  - **Webamp** (MIT, https://github.com/captbaritone/webamp) — the classic-skin
    sprite coordinate table (`packages/webamp/js/skinSprites.ts`) and main-window
    layout (`css/main-window.css`) plus selector mapping (`js/skinSelectors.ts`)
    were transcribed at commit `e6283c0eabe9b1313cd41df16d2875e897719696`, with
    attribution in the file headers. SE-1 promoted the FE-3 transcription into
    `src/skins/SkinSprites.hpp`; the layout constants remain spike-only in
    `spikes/fe3_wsz/MainWindowRenderer.cpp` (rendering is SE-2).
  - **Webamp** (MIT) — SE-1's lenient text parsers (`src/skins/SkinText.cpp`)
    port the *behaviour* of `js/utils.ts` (`parseIni`, `parseViscolors`),
    `js/skinParserUtils.ts` (`getPlaylistStyle`) and `js/regionParser.ts`
    (NumPoints/PointList, <3-point polygon dropping, space/comma separators) at
    the same pinned commit, with the divergences documented in the header.
    `src/skins/SkinSet.cpp` (fallbacks, last-member-wins lookup order) follows
    `js/skinParser.js` / `skinParserUtils.ts` `getFileFromZip`.
  - Webamp is also used, unmodified, as a *test-only* headless renderer
    (`spikes/fe3_wsz/reference/`) to produce the pixel-diff reference; the
    bundle is fetched to the gitignored `third_party/webamp/`, never committed.
  - **Webamp** (MIT) — the `.eqf`/`winamp.q1` container layout used by AU-1's
    `src/audio/EqPreset.cpp` (`packages/winamp-eqf`: the 31-byte header, the
    257-byte name field, the 10 band bytes + preamp byte) is transcribed from
    the *behaviour*/format of that package and its cited public format
    description, with attribution in the file header. The filter math,
    ReplayGain arithmetic and limiter are original. No WCL/Winamp source was
    consulted.
- The Nullsoft `base-2.91.wsz` skin artwork is **not shipped or committed**; the
  FE-3/SE-1 fixtures fetch it locally (MD5-pinned) into gitignored
  `third_party/skins/` via `spikes/fe3_wsz/fetch-skin.sh`. SE-1's 30-skin
  quirk corpus (`scripts/fetch-corpus.sh`, MD5-pinned, per-skin provenance in
  `docs/tickets/SE-1-wsz-loader.md`) likewise lands only in gitignored
  `third_party/skins-corpus/` — each skin remains its author's copyright.
- Qt modules used: QtCore, QtGui, QtWidgets, QtOpenGL, QtOpenGLWidgets, QtTest. QML and the
  copyleft-only Qt modules (Quick3D, Wayland Compositor, …) are excluded.
- Qt is provided by the platform (distro packages on Linux, prebuilt on
  Windows/macOS via CI); relinking obligations are met by shipping it as
  separate shared libraries.
- Acquisition strategy per platform: `docs/spikes/fe0-deps.md`.
- Vendored single-file deps live in the gitignored `third_party/`, fetched by
  `scripts/vendor-deps.sh` with SHA-256 verification.
