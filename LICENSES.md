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
| Qt (Core/Gui/Widgets/OpenGL) | 6.12.0 (CI Linux/macOS) · 6.10.3 (CI Windows) · 6.10.2 (local dev) | LGPL-3.0-only | dynamic | https://code.qt.io/cgit/qt/ |
| libvlc | 3.0.x | LGPL-2.1-or-later | dynamic | https://code.videolan.org/videolan/vlc |
| TagLib | 2.3.2 | LGPL-2.1-or-later OR MPL-2.0 | dynamic | https://github.com/taglib/taglib |
| projectM | 4.1.8 | LGPL-2.1-or-later | dynamic | https://github.com/projectM-visualizer/projectm |
| SQLite (FTS5) | 3.46.x | public domain | static (permitted) | https://sqlite.org/ |
| miniaudio | 0.11.22 | MIT-0 (dual public domain) | header-only | https://github.com/mackron/miniaudio |
| libcurl | 8.18.0 | curl | dynamic | https://curl.se/libcurl/ |
| nlohmann/json | 3.11.3 | MIT | header-only | https://github.com/nlohmann/json |

## Build / test dependencies (not shipped)

| Dependency | Version | License | Link mode | Source |
|---|---|---|---|---|
| Catch2 | 3.7.1 | BSL-1.0 | header/source, build-time only | https://github.com/catchorg/Catch2 |

## Notes

- Qt modules used: QtCore, QtGui, QtWidgets, QtOpenGL, QtTest. QML and the
  copyleft-only Qt modules (Quick3D, Wayland Compositor, …) are excluded.
- Qt is provided by the platform (distro packages on Linux, prebuilt on
  Windows/macOS via CI); relinking obligations are met by shipping it as
  separate shared libraries.
- Acquisition strategy per platform: `docs/spikes/fe0-deps.md`.
- Vendored single-file deps live in the gitignored `third_party/`, fetched by
  `scripts/vendor-deps.sh` with SHA-256 verification.
