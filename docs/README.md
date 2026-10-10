# Llama 5 — Project Documentation

Clean-room Winamp-style music player (working title; see feasibility §6.3 for naming constraints). **Qt 6.12 LTS / C++20** · Windows, macOS, Linux.

**Stack pivot (2026-10-10):** v1 planning targeted Avalonia/.NET; after a framework review the project moved to **Qt 6** — classic Winamp's shapes are window *masks* (`region.txt` → SetWindowRgn/XShape), and Qt is the only mainstream framework with a first-class mask API. Decision record: `feasibility.md` §1a; comparison: `research/frameworks.md`.

## Read in this order

| Doc | Purpose |
|---|---|
| [`feasibility.md`](feasibility.md) | Verdict, decision record, risk matrix, legal posture, phasing |
| [`spec.md`](spec.md) | Technical spec: architecture, formats, features, phases (Qt/C++) |
| [`tickets/`](tickets/) | Per-ticket briefs (AI-consumable task contracts: story, tasks, checkable DoD, out-of-scope, reference paths) |
| [`tickets.md`](tickets.md) | Build backlog index: epics M0–M3, sequencing, sizing, dependency graph |

## Server integration note

Jellyfin + Plex client code is a **port of the maintainer's own koalapaw Android TV clients** (`~/Code/koalapaw/koalapaw-androidtv/{jellyfin-api,plex-api}`) — field-tested clean-room Kotlin; the C++ modules transliterate its wire contracts rather than rediscovering them.

## Research evidence

`research/` contains the seven background reports (2026-10-10) the docs cite:

- `frameworks.md` — the Qt-vs-alternatives comparison that drove the pivot (masks vs alpha, Wayland wall, per-candidate matrix)
- `skins.md` — classic WSZ + modern WAL/Maki formats, skin corpora, legal
- `visuals.md` — vis.h ABI, MilkDrop/projectM, AVS, embedding options
- `audio.md` — engine comparison (libvlc/mpv/GStreamer/BASS), EQ, PCM tap, DSP, tags, search
- `platform.md` — (Avalonia-era) rendering/transparency/clean-room analysis; superseded for stack choice by `frameworks.md`, still cited for clean-room method and Winamp UI semantics
- `servers.md` — Jellyfin + Plex API/auth/playback/scrobble contracts
- `formats.md` — M3U/PLS/B4S/XSPF, NDE library DB, winamp.ini/q1, plugin APIs, feature history
