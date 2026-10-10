# FE-0 · Bootstrap repo & CI

- **Milestone:** M0 · **Est:** 2d · **Depends:** — · **Status:** in-review (all acceptance items met — Linux-CI vendor-deps job added in review; Win/macOS GUI smoke deferred to owner, ctest offscreen self-test is the machine-verifiable proxy)

## Story
The skeleton everything else stands on: a CMake superbuild that produces a runnable (empty, frameless) Qt window on all three platforms, a green CI matrix on pinned runner images, and the dependency vendoring script that turns the license inventory into enforceable fact. After this ticket, every other ticket is "open the brief, build, run tests" with zero environment archaeology.

## Tasks
- [x] CMake superbuild (≥3.24): `CMakeLists.txt` root + `src/{ui,playback,library,servers,skins,interop}` + `tests/` subdirs; C++20, Qt 6.12 target / **`find_package(Qt6 6.10 REQUIRED)` minimum** (local dev machines may carry 6.10.x — see Notes; Widgets + Gui + OpenGL only — no QML, no GPL-only modules), ccache, presets for msvc/gcc/clang.
- [x] Minimal `main.cpp`: QApplication, one frameless toplevel (275×116 — the future main window), `show()`. No skin code, no engine — just the window.
- [x] Dependency acquisition: decide **vcpkg manifest vs. distro/brew/prebuilt-Qt** per platform and record the choice in `docs/spikes/fe0-deps.md` with versions: Qt 6.12.x, libvlcpp (or raw libvlc headers), TagLib 2.3.x, projectM 4.1.x, SQLite, miniaudio (header), libcurl, a JSON DOM (nlohmann or simdjson — pin one, the same one FE-5 uses).
- [x] Vendoring script `scripts/vendor-deps.sh` (or per-OS variants): pinned versions, verification hashes, installs to gitignored `third_party/`; must be re-runnable and idempotent. Native libs stay separate files (LGPL discipline) — never single-file-merged, never static-linked into the app binary.
- [x] CI real build: replace the placeholder matrix with pinned runner images (`windows-2022`, `ubuntu-24.04`, `macos-14`), `actions/checkout@v5`, CMake configure + build + a smoke `ctest` run per platform. Also run `scripts/license-audit.sh` after `third_party/` exists (extend it with the manifest check). **CI-name/protection coupling:** branch protection on `master` requires status checks by exact job name — `license audit` and `secrets scan`. Keep those two job `name:` values byte-identical in the rewritten `ci.yml`; if you must rename or restructure them, re-sync protection in the same change via `PUT /repos/idontwantyourspamthanks/koalaamp/branches/master/protection` (else required contexts never report and non-admin/bot merges hang on "Expected — Waiting for status"). The build placeholder job is *not* a required check and may be renamed freely.
- [x] Test scaffolding: one trivially-passing `ctest` test per platform (proves the harness works before anything depends on it); GoogleTest or Catch2 vendored — pick one, pin it.
- [x] `LICENSES.md` generated (human-maintained from now on): every dependency, its license, link mode (dynamic), source URL, pinned version.
- [x] `.clang-format` (pick a style — e.g. LLVM-based, 100-col — consistency beats taste here), apply to the skeleton, add format check to CI as non-blocking warning.

## Acceptance
- `cmake --preset <platform> && cmake --build --preset <platform>` produces a binary that opens a 275×116 frameless window on Win10+, macOS 14+, and Linux X11 (manual smoke — paste screenshots in the PR).
- CI matrix green on all three pinned runners with a real compile (not the placeholder echo); ctest runs and passes ≥1 test per platform.
- `scripts/vendor-deps.sh` runs clean from a fresh clone on at least Linux CI + one dev machine; re-run is a no-op (idempotency asserted).
- `bash scripts/license-audit.sh` exits 0 *with dependencies present* (i.e., the audit still passes when LGPL libs are actually linked — the case it was fixed for).
- `LICENSES.md` exists with the full inventory: Qt/libvlc/TagLib/projectM/SQLite/miniaudio/libcurl/JSON-lib/test-framework.
- No `-latest` runner labels remain in `ci.yml`; `actions/checkout@v5`.

## Out of scope
- Anything skin-related beyond the empty window's *size* (FE-1/SE-1 own real windows and skins).
- libvlc/projectM *integration* — only their acquisition/build wiring (FE-2/FE-14 integrate).
- The window-manager, engine, or IPC (spec §2.1 interfaces get declared, not implemented — a header per module with the interfaces stubbed, so later tickets have seams to fill).
- macOS notarization, packaging, installers (PC-1/V1-0).
- AGENTS.md/repo policy (already shipped).

## References
- `docs/spec.md` §2.1-2.3 — module layout, layering rules, interface contracts to stub
- `docs/feasibility.md` §6 — the LGPL dynamic-linking discipline the vendoring script must enforce; §1a license column
- `docs/tickets.md` FE-1/FE-2/FE-5/FE-14 — the four spikes that unblock the moment this lands; their dependency assumptions define what "skeleton complete" means
- `AGENTS.md` — clean-room rules the CI license-audit enforces
- Qt licensing: LGPLv3 modules only (QtWidgets/QtGui/QtOpenGL); GPL-only modules (Quick3D, Qt Wayland Compositor) must not appear in the dependency set

## Notes / risks
- The deps decision (vcpkg vs. system) is the real work here; on Windows vcpkg is near-mandatory, on Linux distro packages are saner for Qt — per-platform asymmetry is fine, *documented* asymmetry is the deliverable.
- Pin exact versions everywhere now; upgrading becomes a deliberate commit later, never a surprise CI failure (the `ubuntu-latest → Ubuntu 26` migration on Oct 19 2026 is exactly this class of problem).
- Branch protection on `master` requires status checks by exact job name (`license audit`, `secrets scan`) — the workflow rewrite must preserve those `name:` values or re-sync protection (see the coupling note in Tasks). The ticket worker has no other way to know this; it's in the brief because the failure mode (PRs hanging on "Expected — Waiting for status") surfaces far from the cause.
- Keep the build hermetic enough that an AI worker in a fresh clone reaches "builds + tests pass" without human hints — that property is the acceptance test for the whole ticket.
- **Local Qt version (this dev machine):** installed Qt is 6.10.2 — do **not** hard-pin `find_package(Qt6 6.12 REQUIRED)`: that fails locally on a version mismatch, not a code bug. Accept `>=6.10` in CMake (Widgets/Gui/OpenGL APIs we use are stable across that range; setMask and QOpenGLWidget predate 6.0), let CI install its pinned Qt 6.12 from its own source, and record the local/CI divergence in `docs/spikes/fe0-deps.md`. If a genuinely 6.12-only API ever appears, that's the moment to bump the local toolchain (aqtinstall) deliberately.
