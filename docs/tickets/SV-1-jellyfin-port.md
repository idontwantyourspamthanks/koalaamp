# SV-1 · Jellyfin client port (auth / browse / search)

- **Milestone:** M3 · **Est:** 4d · **Depends:** LI-2, FE-5 · **Status:** unstarted

## Story
A C++ Jellyfin `ITrackSource` that authenticates, discovers servers, and browses/searches music libraries — ported from the maintainer's field-tested koalapaw Android code rather than written against the API docs from scratch. This is transliteration work: the wire contracts, error handling, and header discipline are already solved in Kotlin; our job is a faithful C++ rendering of them.

## Tasks
- [ ] Port `AuthInterceptor` logic: `Authorization: MediaBrowser Client="KoalaAmp" Device=... DeviceId=<hash(machineId,userId)> Version=...` on every request; 401 → re-auth; **no** `X-Emby-Token`, **no `api_key` query param — anywhere, including the WS handshake**.
- [ ] Port `POST /Users/AuthenticateByName` flow (username/password → `AccessToken`, `ServerId`, session info).
- [ ] Port `UdpServerDiscovery` (broadcast probe + response parse) + manual-URL fallback.
- [ ] Optional: QuickConnect flow (koalapaw implements it — port if cheap, else defer).
- [ ] Browse: port `musicAlbumsWindow` / `musicArtistsWindow` / A-to-Z letter-offsets (koalapaw `JellyfinGateway.kt:689-721` has the exact query shapes: `IncludeItemTypes`, `Recursive`, `SortBy`, paging).
- [ ] Search: `/Items` queries with `SearchTerm`; map results to our `Track` model.
- [ ] Artwork: `/Items/{id}/Images/Primary` fetch + disk cache (respect server cache headers).
- [ ] Gateway interface in C++ mirroring koalapaw's `JellyfinGateway` seam (auth state, browse, search, errors as result types — no exceptions across the seam).
- [ ] Connection-manager UI (skinned, minimal): server list, add-by-URL, login.
- [ ] Record request/response fixtures for CI (Jellyfin 10.10 + 10.12 containers).

## Acceptance
- Against live Jellyfin 10.10 **and** 10.12 containers: authenticate, UDP-discover, browse albums/artists (with A-to-Z offsets), search "pink" returns expected tracks, artwork caches.
- Static assertion in CI: a grep of the jellyfin module finds zero occurrences of `X-Emby-Token` and zero `api_key` (query-auth) usage — headers only, per server 12.0 removal.
- Fixture-based unit tests replay every recorded interaction offline.
- Auth headers verified for the *WS client handshake path* too (not just REST).

## Out of scope
- Playback, progress reporting, remote control (SV-2).
- Transcode decisions (SV-2's fallback logic).
- Ratings/favorites sync, server playlists (Phase 2.5).
- Multi-server *simultaneous* connections (single active server per source is v1).

## References
- `spec.md` §8.1-8.2 — the governing contracts
- **koalapaw source (the port base):** `~/Code/koalapaw/koalapaw-androidtv/jellyfin-api/src/main/kotlin/com/koalapaw/jellyfin/` — `client/AuthInterceptor.kt`, `client/JellyfinApi.kt`, `client/JellyfinGateway.kt` (esp. music windows at lines 689-721), `client/UdpServerDiscovery.kt`
- `docs/research/servers.md` §1 — API/auth evidence incl. the 10.12/12.0 header deprecations and the per-DeviceId token rule
- Jellyfin test containers: pin exact image digests in the test harness

## Notes / risks
- The one *new* thing vs koalapaw: our DeviceId derivation must be stable per (machine, user) because Jellyfin invalidates the old token when a DeviceId re-authenticates — the koalapaw comment on device-identity (JellyfinGateway.kt:98) documents this trap; port the derivation, not just the header.
- Kotlin coroutines → C++: use the gateway result-type seam (no threads leaked across the interface); an internal thread pool per server connection is fine.
