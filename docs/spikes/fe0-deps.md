# FE-0 spike — dependency acquisition & build decisions

- **Status:** decided (2026-10-10) · **Owner:** FE-0 · **Supersedes:** —
- **Enforced by:** `deps/dependencies.json` → `scripts/license-audit.sh`

This records the FE-0 dependency decisions. Scope is **acquisition wiring, not
integration**: FE-0 builds only the Qt skeleton; libvlc/TagLib/projectM are
wired by FE-2/FE-5/FE-14. Where a decision is deferred, it says so.

## Decision: per-platform acquisition (hybrid, not one-size)

There is no single acquisition strategy that is sane on all three platforms.
Qt is the hard case: on Linux, distro packages are the path of least
resistance; on Windows, vcpkg is near-mandatory for a native C++ stack; on
macOS, Homebrew. Native, hopelessly-cross-compiled libraries (libvlc, TagLib,
projectM) follow the *same* per-platform choice as Qt — whatever ships them
well — rather than a second mechanism.

| Platform | Qt | Native deps (libvlc/TagLib/projectM/SQLite/libcurl) | Test framework |
|---|---|---|---|
| **Linux** (ubuntu-24.04, dev) | distro packages / CI `install-qt-action` | distro packages (`libvlc-dev`, `libtag1-dev`, `libsqlite3-dev`, `libcurl4-openssl-dev`); projectM from source | FetchContent |
| **Windows** (windows-2022) | `install-qt-action` (prebuilt MSVC) | **vcpkg manifest** for TagLib/SQLite/curl/zlib; **official VLC 3.0.21 win64 SDK** for libvlc (vcpkg has no port), DLLs/plugins staged next to the exe | FetchContent |
| **macOS** (macos-14) | `install-qt-action` (prebuilt clang) | Homebrew (`vlc`, `taglib`, `projectm`, `sqlite`, `curl`) | FetchContent |

**Why not vcpkg everywhere?** vcpkg's Qt port is heavyweight and its LGPL Qt
packages are a licensing/reporting minefield; distro/brew/prebuilt Qt is more
direct and better supported. **Why not system packages on Windows?** There are
none. The asymmetry is deliberate and documented (the ticket's Notes flag it).

**CI Qt on Linux as well as Windows/macOS:** Ubuntu 24.04's Qt is 6.4, below
our 6.10 floor, so CI installs a pinned Qt on all three runners via
`jurplel/install-qt-action@v4`. The pin is **per platform**: **6.12.0** on
ubuntu-24.04 and macos-14, **6.10.3** on windows-2022. Qt 6.11/6.12 Windows
packages are published (`qt6_6120/qt6_6120_msvc2022_64/Updates.xml` exists) but
not yet installable by the action's pinned `aqt` 3.3.0, which still expects the
old flat Windows repository layout — so Windows sits at the newest version the
toolchain can actually install. Bump Windows to 6.12.x when aqt catches up;
until then the matrix is deliberately non-uniform and that is recorded here.

## Decision: Qt floor `>= 6.10`, target line 6.12 LTS

`find_package(Qt6 6.10 REQUIRED COMPONENTS Widgets Gui OpenGL)`.

The project targets **Qt 6.12 LTS** (spec §1.1); CI installs 6.12.0 on Linux
and macOS, and 6.10.3 on Windows (see above), while the reference dev machine
carries **6.10.2** (distro). The Widgets/Gui/OpenGL APIs used through M0
(`QWidget::setMask`, `QOpenGLWidget`, frameless
flags) predate 6.0 and are stable across 6.10→6.12, so accepting `>= 6.10`
avoids failing local configure on a *toolchain* mismatch, not a code bug.
If a genuinely 6.12-only API is ever needed, that is the moment to bump the
local toolchain deliberately (aqtinstall) and raise the floor — never before.

**Divergence is expected, not drift:** CI = 6.12.0 (Win: 6.10.3),
local = 6.10.2. The build log prints which line was found.

## Decision: raw libvlc C API, not libvlcpp

The ticket allows libvlcpp *or* raw libvlc headers. We pick **raw headers**
(`libvlc/libvlc.h`, from the libvlc 3.0.x package). libvlcpp is an LGPL,
header-only C++ wrapper: choosing it would compile LGPL code into our binary
and blur the dynamic-linking discipline. The C API is what FE-2 needs
(`libvlc_audio_set_callbacks`, `libvlc_media_new_callbacks`) and carries no
extra license surface.

## Decision: JSON DOM = nlohmann/json 3.11.3

Pinned single choice, shared with FE-5/FE-14. nlohmann/json is header-only MIT,
ergonomic for the koalapaw wire contracts, and avoids simdjson's parser-only
constraints. Vendored as a single header.

## Decision: test framework = Catch2 v3.7.1 (FetchContent, URL+SHA256)

Catch2 v3.7.1 is pinned by `URL` + `URL_HASH` in `tests/CMakeLists.txt` and
fetched into the build tree at configure time. It is **not** vendored into
`third_party/` (which stays empty this ticket) and not built by hand — one
`FetchContent_MakeAvailable` gives `Catch2::Catch2WithMain` and
`catch_discover_tests`. GoogleTest was the alternative; Catch2's single-header
heritage and CTest discovery tipped it.

## Decision: vendor script scope

`scripts/vendor-deps.sh` vendors only dependencies that are safe, single-file,
hash-pinned downloads (currently **miniaudio 0.11.22**, **nlohmann/json
3.11.3**), installed into the gitignored `third_party/`. It is idempotent
(`--check` verifies, `--clean` removes). Native libraries are *not* single-file
merged or statically linked (LGPL discipline, feasibility §6); their pinned
versions live in `deps/dependencies.json` and are acquired per the table above.
`third_party/` is empty in the committed tree by design.

Deferred to FE-2/FE-5/FE-14: adding libvlc/TagLib/projectM sources + hashes to
the script once their build wiring exists. CI-1 now fetches the pinned Windows VLC
3.0.21 SDK in `ci.yml`; the official binary zip has no SDK, the `.7z` does.

## Decision: ccache, style, warnings

- **ccache** is used opportunistically (`CMAKE_CXX_COMPILER_LAUNCHER` set only
  if the binary is found) — never required.
- **C++20**, `CMAKE_CXX_EXTENSIONS OFF`; warnings `-Wall -Wextra -Wpedantic
  -Wshadow -Wnon-virtual-dtor` (MSVC `/W4 /permissive-`).
- **.clang-format**: LLVM base, 100 columns, 4-space indent. Consistency over
  taste. CI runs `clang-format --dry-run --Werror` as a **non-blocking** step.

## CI / branch-protection coupling (load-bearing)

Branch protection on `master` requires status checks by **exact job name**:
`license audit` and `secrets scan`. The FE-0 rewrite keeps those two job
`name:` values **byte-identical**; only the build job changed (placeholder →
3-platform matrix). No protection re-sync was needed. If those jobs are ever
renamed, re-sync protection in the same change via
`PUT /repos/idontwantyourspamthanks/koalaamp/branches/master/protection` —
otherwise required contexts never report and non-admin merges hang on
"Expected — Waiting for status".

## Pinned versions (single source of truth: `deps/dependencies.json`)

Qt 6.12.0 (CI Linux/macOS) · 6.10.3 (CI Windows) · 6.10.2 (local) ·
libvlc 3.0.x · TagLib 2.3.2 · projectM 4.1.8 ·
SQLite 3.46.x · miniaudio 0.11.22 · libcurl 8.x · nlohmann/json 3.11.3 ·
Catch2 3.7.1. All upgrades are deliberate commits, never surprise CI drift
(the `*-latest` → `ubuntu-24.04` class of problem is exactly what pinning
prevents; no `-latest` labels remain in `ci.yml`).

## Deferred / open

- Integration of the native deps — FE-2 (libvlc), FE-5 (libcurl/JSON), FE-14
  (projectM). FE-0 stops at acquisition wiring + the Qt skeleton.
- Windows/macOS manual smoke (window screenshots) requires a human; CI proves
  compile + tests only.
- `vendor-deps.sh` running in CI: deferred with the native deps (CI builds only
  the Qt skeleton while `third_party/` stays empty). Verified locally instead.
