# KoalaAmp

*A clean-room, cross-platform music player in the spirit of classic Winamp — built with Qt 6.*

Renders the classic Winamp 2 skin format pixel-faithfully (the weird and wonderful ones included, via real OS window masks), plays MilkDrop presets through projectM, and speaks to your **local library, Jellyfin, and Plex**. Windows, macOS, Linux.

> **Naming:** "KoalaAmp" is the working title, pending the RL-1 trademark check. It deliberately avoids the "Winamp" and "Jelly[word]" constraints that rule out other candidates — and it's a deliberate `Koala*` family sibling to KoalaPaw (the maker's Android TV Jellyfin/Plex client): expect shared koala branding and "from the maker of KoalaPaw" cross-recognition; if the paid-TV-app and free-desktop-player brands should stay visually distinct, that's a pre-listing decision, not a code one. This project is not affiliated with Winamp Group SA, Nullsoft, Jellyfin, or Plex — "Winamp" is their trademark; we merely read their skin format.

**Status:** planning complete, pre-implementation. The full plan:

- [`docs/README.md`](docs/README.md) — start here (doc map)
- [`docs/feasibility.md`](docs/feasibility.md) — GO verdict, stack decision record (why Qt), risks, legal posture
- [`docs/spec.md`](docs/spec.md) — technical spec: architecture, formats, features
- [`docs/tickets.md`](docs/tickets.md) — build backlog M0–M3
- [`docs/tickets/`](docs/tickets/) — per-ticket briefs (self-contained task contracts)
- [`docs/research/`](docs/research/) — 7 evidence reports with per-claim citations

## Clean-room rules (see `AGENTS.md`)

No code from the 2024 Winamp source release (WCL license) or GPL projects is read or copied; the skin format is reimplemented from public 1998-era specs and MIT-licensed references (Webamp) with attribution. Server integration is a port of the maintainer's own field-tested Jellyfin/Plex client code.

## Build

Nothing to build yet — the first implementation ticket is FE-0 (repo bootstrap + CI matrix). AI/human contributors: read [`AGENTS.md`](AGENTS.md) first.
