# CI-1 · Windows compile coverage for the playback path

- **Milestone:** M1 (CI hardening; blocks nothing, prevents owner-desk failures) · **Est:** 2d · **Depends:** FE-4 · **Status:** in-progress — VLC 3.0.21 SDK pin + Windows CMake/CI wiring landed; first Windows compile pass pending

## Story
Windows CI builds no `koalaamp_playback_core`, `koalaamp_vlc`, or `src/interop/vlc/` — `find_package` for libvlc fails on the runner, so ~1,500 lines of the app's core playback path have **zero Windows compile coverage**. Every CI-only surprise this session lived on Windows (Ws2_32, WSAStartup, zlib1.dll, Qt-DLL discovery, the §-test-name mojibake); the player's most load-bearing code is exactly the part the Windows runner cannot see. Goal: those targets *compile and link* on Windows CI so a Windows-only break fails at PR time, not on the owner's desk. Tests stay Linux-gated (running libvlc on the runner is a separate, later question).

## Tasks
- [ ] libvlc on Windows CI: acquire headers (+ link stubs) — vcpkg has `libvlc`? research; else the official VLC 3.0.x SDK zip (VideoLAN) fetched pinned into third_party via a vendor-deps Windows section, or a vcpkg overlay port. Record the choice and its maintenance posture in the ticket (pinned source, not floating).
- [ ] Wire CMake: make `find_package`/pkg-config equivalent resolve on Windows (CMAKE_PREFIX_PATH to the SDK; the existing `KOALAAMP_HAS_LIBVLC` guard flips true there).
- [ ] Confirm the Windows build compiles `src/interop/vlc/*` and `src/playback/PlaybackCore.cpp` (the FE-4/FE-2 targets) — expect to fix any Windows-only compile issues found (they exist; that's the point of the ticket).
- [ ] Add a CI-visible marker (test or build step output) asserting the playback targets built on all three platforms — so the gap can't silently reopen.
- [ ] The Ws2_32/WSAStartup patterns already in `src/servers` are the template for what this ticket will likely surface in the vlc path — fix as found, with the same guarded-per-platform style.

## Acceptance
- Windows CI job output shows `koalaamp_vlc`/`koalaamp_playback_core` targets compiling (marker assertion green on all 3 platforms).
- No behavior change on Linux/macOS (suite unchanged: 93/93).
- Any Windows-only fixes are guarded (`if(WIN32)` style) and documented in-file.
- Deps decision recorded (vcpkg vs SDK zip) with version pin + rationale in `docs/spikes/`-style note or the ticket file.

## Out of scope
- *Running* playback tests on Windows CI (libvlc runtime + plugins on the runner — later ticket if wanted).
- macOS equivalents (brew vlc already provides headers; coverage gap is Windows-specific).
- Any app-code features.

## References
- `tests/CMakeLists.txt:50,62` — the guards that gate the vlc/playback targets out of Windows today
- `docs/spikes/fe0-deps.md` — the per-platform acquisition table this extends
- The session's Windows-only failure chain: Ws2_32 (jellyfin), WSAStartup, zlib1.dll/Qt-DLL discovery PATH, §-name mojibake — all PR-time catches that this ticket generalizes into compile coverage

## Notes / risks
- vcpkg's libvlc port (if present) tracks a specific 3.x — acceptable if pinned; the SDK zip is the more deterministic option but needs hosting/verification discipline. Prefer whichever a maintainer can re-fetch in 6 months.
