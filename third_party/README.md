# third_party/ — vendored dependencies

This directory is **gitignored** and populated by `scripts/vendor-deps.sh`
(`third_party/README.md` is the only tracked file). Do not commit vendored
code here.

- Pinned single-file deps (miniaudio, nlohmann/json) are fetched by the
  script with SHA-256 verification.
- libprojectM 4.2.0-dev (LGPL) is fetched as pinned source and built by the
  script into a **shared** library under `projectm/` (dynamic link only, never
  committed, never static). The other native libraries (Qt, libvlc, TagLib,
  SQLite, libcurl) are acquired per platform — system packages, vcpkg, or
  prebuilt Qt — as decided in `docs/spikes/fe0-deps.md`.
- The full inventory lives in `deps/dependencies.json`, is mirrored in the
  repo-root `LICENSES.md`, and is enforced by `scripts/license-audit.sh`.
