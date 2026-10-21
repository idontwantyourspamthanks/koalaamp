# CO-1 · Platform integration pass 1

- **Milestone:** M2 · **Est:** 4d · **Depends:** SE-3
- **Status:** in-review — code complete; Windows HKCU round-trip and Linux registration smoke pass; macOS association is the packaging plist and live hotkey registration remains a desktop check

## Story
A launch that already has KoalaAmp running should not produce a second player. Files and
`Play`/`Enqueue` verbs from the desktop should reach the one running playlist, and the player
should be reachable without hunting its windows: tray menu, global hotkeys, and the OS file
association verbs.

## Tasks
- [x] CLI transport verbs: positional files plus `--play` (default) and `--enqueue`; `--self-test`
  and `--no-single-instance` stay local for tests. — `src/main.cpp`; dispatch appends through
  `PlaylistWindow::addMediaFiles` and starts the first new row only for `--play`.
- [x] Per-user single-instance IPC: first process owns a `QLocalServer`, later launches forward
  their argv as a JSON array and exit 0; a 1.5 s timeout falls back to becoming the primary.
  Commands are queued until the playback/playlist wiring exists. — `src/platform/SingleInstance.{hpp,cpp}`,
  `src/main.cpp`; no token/credential crosses the socket.
- [x] Tests: a second `SingleInstance` forwards, a primary receives, and a failed forward becomes
  primary. — `tests/single_instance_test.cpp`; smoke: `koalaamp --enqueue <file>` against a running
  `--null-audio` instance exits 0 and raises the playlist.
- [x] `QSystemTrayIcon`: transport actions (play/pause, next, previous), show/hide the window set,
  and exit; no second tray icon from a forwarded launch (the forwarder exits before creating UI). —
  `src/ui/TrayIcon.{hpp,cpp}`; `WindowSet::setVisible/anyVisible`; `se4_part_a_test` covers the
  hide/show action. `--self-test` skips the tray entirely.
- [x] Global hotkeys: QHotkey-backed native registration for Play/Pause, previous/next, stop and
  volume; registered only by the primary process and skipped under `--self-test`. Failures are
  reported per binding. Preferences/config surface remains PC-1. — `src/platform/GlobalHotkeys.{hpp,cpp}`;
  QHotkey pinned in the root CMake.
- [x] File associations: Windows HKCU classes + `Play`/`Enqueue` verbs, Linux user `.desktop` +
  `mimeapps.list`, macOS bundle `Info.plist` document types. Registration is explicit via
  `--register-file-types` / `--unregister-file-types`. — `src/platform/FileAssociations.{hpp,cpp}`,
  `packaging/macos/Info.plist`; pure manifest tests in `file_associations_tests`.
- [x] Single-instance payload carries the chosen verb end to end: association → CLI → socket →
  playlist, including multiple files. Windows/Linux registration commands are unit/smoke tested;
  macOS is delivered by the bundle plist.
- [ ] Acceptance evidence on all three OSes; Windows/macOS are not e2e until file association is
  installed, so keep the headless unit/smoke path green. Linux registration/unregistration is
  smoke-tested with temporary `XDG_CONFIG_HOME`/`XDG_DATA_HOME`; macOS plist is validated with
  `plistlib`; Windows HKCU registration/unregistration is a CI test (`file_associations_tests`).

## Acceptance / Definition of Done
- `single_instance_tests` passes on Linux, macOS and Windows.
- `koalaamp --enqueue /tmp/a.flac` while a primary is running exits 0 and adds one playlist row.
- `koalaamp --play /tmp/a.flac` starts that row; multiple files preserve order.
- A second launch with no files raises the existing player and exits 0.
- Tray and hotkeys are exercised by an offscreen test/script on each OS; association verbs are
  verified by launching the exact registered command lines.
- No credential, server token or settings content is written to the local socket or command line.

## Out of scope
- Restoring playback position/queue across launches (PC-1).
- Protocol handlers (`jellyfin://`, `plex://`) and server playback forwarding.
- Auto-update and installer work (RL-1/V1-0).

## References
- `docs/spec.md` §11 — platform integration
- `src/main.cpp` — QCommandLineParser and window/core composition
- `src/platform/SingleInstance.hpp` — socket contract
- `src/ui/PlaylistWindow.hpp` — `addMediaFiles` / `playRow`
- `docs/tickets/SE-3-playlist-window.md` — playlist verbs and autosave

## Notes / risks
- The local socket name is derived from the per-user `AppConfigLocation` hash, so two users on one
  machine never share a primary. `UserAccessOption` is set on platforms that support it.
- The forwarder must not open a second window set: `main` returns before constructing any window.
- A stale socket is removed only after a connect attempt fails; an active primary is never unlinked.
- `QLocalServer`/`QLocalSocket` is the portability seam; no raw Unix-domain/TCP path is used.
- Initial global-hotkey map is `Ctrl+Alt+Home` play/pause, `Ctrl+Alt+Left/Right` previous/next,
  `Ctrl+Alt+End` stop, `Ctrl+Alt+Up/Down` volume. QHotkey reports per-binding failures (reserved or
  duplicate X11 grabs); PC-1 will make the map configurable.
- File registration is never automatic. `--register-file-types` writes HKCU classes on Windows,
  a user `.desktop` + `mimeapps.list` on Linux, and errors on macOS because the document types live
  in `packaging/macos/Info.plist`. `--unregister-file-types` reverses the user-level pieces.
