# FE-5 · Spike: koalapaw jellyfin-api port skeleton

- **Milestone:** M0 · **Est:** 3d · **Depends:** FE-0 · **Status:** in-review

## Story
De-risk the single biggest *assumption* in M3 before M1 spends weeks building on it: that the koalapaw Kotlin gateway ports cleanly to C++ over libcurl + a JSON DOM. One auth flow, one discovery, one browse call — enough to prove the transliteration pattern (result-type seams, header discipline, coroutines → thread pool) and leave a recorded fixture harness behind.

## Tasks
- [x] Choose and pin: libcurl (version), JSON DOM library (nlohmann or simdjson — pick once for the whole codebase here). → **libcurl 8.18.0** + **nlohmann/json 3.11.3** (`docs/spikes/koalapaw-port.md`); libcurl pinned in `deps/dependencies.json`.
- [x] Port `UdpServerDiscovery` (broadcast + response parse) to C++.
- [x] Port `AuthInterceptor` header building: `Authorization: MediaBrowser Client="KoalaAmp" Device=... DeviceId=<derived> Version=...`; per-user DeviceId derivation — implemented as `sha256(machineId:userKey)`; **koalapaw actually persists a random UUID per profile, not a hash** (documented in the spike report).
- [x] Port `POST /Users/AuthenticateByName` → parse `AccessToken`, `ServerId`, session.
- [x] Port ONE browse call end-to-end: albums window (`JellyfinGateway.kt:689-706` — `IncludeItemTypes=MUSIC_ALBUM`, `Recursive`, paging) → C++ structs.
- [x] Wrap each call in the gateway result-type seam (no exceptions crossing the boundary; an internal worker thread is fine).
- [x] Stand up Jellyfin test containers (pinned digests: 10.10.7 / 10.11.11 / 12.0 — no stable 10.12 exists) and a smoke binary that runs auth → discovery → browse against them.
- [x] Record request/response fixtures to files for offline replay.

## Acceptance
- Smoke binary: authenticate (MediaBrowser headers only), UDP-discover the container, list music albums from C++ — printed to stdout with album names.
- Fixtures for all three interactions replay offline through the same C++ code path (unit test).
- A short findings note in `docs/spikes/koalapaw-port.md`: what transliterated 1:1, what needed re-shaping (e.g. coroutine cancellation → Cancellation tokens), and the chosen JSON/libcurl pins.
- Zero `X-Emby-Token` / `api_key` strings in the spike source (grep assertion in the test).

## Out of scope
- Full browse/search/artwork surface (SV-1).
- Playback, progress, WS remote control (SV-2).
- Plex module entirely (SV-3).
- Any UI.

## References
- `spec.md` §8.1-8.2 — interface contracts, header rules
- koalapaw: `~/Code/koalapaw/koalapaw-androidtv/jellyfin-api/src/main/kotlin/com/koalapaw/jellyfin/client/{AuthInterceptor,JellyfinApi,JellyfinGateway,UdpServerDiscovery}.kt`
- `docs/research/servers.md` §1.2 — auth scheme evidence, 10.12/12.0 deprecations, per-DeviceId rule

## Notes / risks
- The DeviceId derivation is the one silent breaker (wrong derivation = token invalidation on re-auth). Port it verbatim and test a re-login loop (auth twice in a row succeeds).
