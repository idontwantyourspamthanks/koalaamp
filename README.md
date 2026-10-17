# KoalaAmp

*A clean-room, cross-platform music player in the spirit of classic Winamp — built with Qt 6.*

Renders the classic Winamp 2 skin format pixel-faithfully (the weird and wonderful ones included, via real OS window masks), plays MilkDrop presets through projectM, and speaks to your **local library, Jellyfin, and Plex**. Windows, macOS, Linux.

> **Naming:** "KoalaAmp" is the working title, pending the RL-1 trademark check. It deliberately avoids the "Winamp" and "Jelly[word]" constraints that rule out other candidates — and it's a deliberate `Koala*` family sibling to KoalaPaw (the maker's Android TV Jellyfin/Plex client): expect shared koala branding and "from the maker of KoalaPaw" cross-recognition; if the paid-TV-app and free-desktop-player brands should stay visually distinct, that's a pre-listing decision, not a code one. This project is not affiliated with Winamp Group SA, Nullsoft, Jellyfin, or Plex — "Winamp" is their trademark; we merely read their skin format.

**Status:** M1 complete — the classic skin engine (player/EQ/playlist windows, pixel-identical to the Webamp oracle), the audio core (10-band EQ/DSP, gapless playback, tag-driven ReplayGain) and playlist persistence (M3U/M3U8/PLS/B4S/XSPF/ASX/WPL + autosave) all work, with CI green on Windows/macOS/Linux. M2 — library scanner, projectM visualizer window, Winamp migration — is next. The full plan:

- [`docs/README.md`](docs/README.md) — start here (doc map)
- [`docs/feasibility.md`](docs/feasibility.md) — GO verdict, stack decision record (why Qt), risks, legal posture
- [`docs/spec.md`](docs/spec.md) — technical spec: architecture, formats, features
- [`docs/tickets.md`](docs/tickets.md) — build backlog M0–M3
- [`docs/tickets/`](docs/tickets/) — per-ticket briefs (self-contained task contracts)
- [`docs/research/`](docs/research/) — 7 evidence reports with per-claim citations

## Clean-room rules (see `AGENTS.md`)

No code from the 2024 Winamp source release (WCL license) or GPL projects is read or copied; the skin format is reimplemented from public 1998-era specs and MIT-licensed references (Webamp) with attribution. Server integration is a port of the maintainer's own field-tested Jellyfin/Plex client code.

## Build

Requires CMake ≥ 3.24, a C++20 compiler (gcc 13+/clang 17+/MSVC 2022), and
Qt ≥ 6.10 (Widgets/Gui/OpenGL; CI pins 6.12.0 — see
[`docs/spikes/fe0-deps.md`](docs/spikes/fe0-deps.md)).

```sh
cmake --preset linux-gcc      # or linux-clang / windows-msvc / macos-clang
cmake --build --preset linux-gcc
ctest --preset linux-gcc
```

The binary is `build/<preset>/src/koalaamp`.

## Run

```sh
./run.sh                          # builds (no-op when up to date), then runs
./run.sh --skin path/to/skin.wsz  # any .wsz/.zip, or an extracted skin folder
./run.sh --null-audio             # run silent (no audio device)
./run.sh --self-test              # construct the window set, map, exit 0
./run.sh --no-build               # skip the build, run the existing binary
```

`run.sh` picks the build preset, builds if needed, and uses the skin you last
used — or fetches the classic base skin on a fresh clone, since `third_party/`
is not committed. By hand it is:

```sh
cmake --preset linux-gcc && cmake --build --preset linux-gcc
./build/linux-gcc/src/koalaamp --skin third_party/skins/base-2.91.wsz
```

More skins: `scripts/fetch-corpus.sh` fetches the 30-skin test corpus into
`third_party/skins-corpus/`.

Getting music in: **drag files onto the Playlist Editor**, or right-click it for
*Add file(s)…* / *Open playlist…* — the playlist's ADD menu and its eject button
open a dialog that appends to the list. The player window's own eject button (or
`L`) opens a media dialog and plays straight away instead. Double-click a row to
play it. Playback needs libvlc at build time (the configure log reports
`KOALAAMP_HAS_LIBVLC`); without it you get the skin windows only.

Headless smoke (what CI runs), plus the test suite:

```sh
QT_QPA_PLATFORM=offscreen ./run.sh --self-test   # construct the set, map, exit 0
ctest --preset linux-gcc                         # 201 tests
```

Notes: the windows are frameless and OS-masked, and on Wayland the app asks for
XWayland (`xcb`) so it can position and shape them. The player, EQ and playlist
are separate toplevels that the compositor places — they start on top of one
another, so drag them apart; docking/snapping then keeps the group together and
the arrangement is remembered per skin. Window scale follows the screen's device
pixel ratio (a 275×116 window is 398×168 at 1.447×). State lives under
`~/.config/KoalaAmp/` (skin DB: last skin + per-skin layout) and
`~/.local/share/KoalaAmp/` (playlist autosave).

AI/human contributors: read [`AGENTS.md`](AGENTS.md) first.
