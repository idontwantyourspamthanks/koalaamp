# SV-1 · Jellyfin client port (auth / browse / search)

- **Milestone:** M3 · **Est:** 4d · **Depends:** LI-2, FE-5 · **Status:** in progress — header
  discipline, `AuthenticateByName`, discovery, browse (albums + artists + A–Z offsets) and
  `/Items` search onto `Track` have landed and are fixture-replayed. Open: artwork cache, the
  `IServerSource` seam, the connection-manager UI, live-container fixtures.

## Story
A C++ Jellyfin `ITrackSource` that authenticates, discovers servers, and browses/searches music libraries — ported from the maintainer's field-tested koalapaw Android code rather than written against the API docs from scratch. This is transliteration work: the wire contracts, error handling, and header discipline are already solved in Kotlin; our job is a faithful C++ rendering of them.

## Tasks
- [x] Port `AuthInterceptor` logic: `Authorization: MediaBrowser Client="KoalaAmp" Device=... DeviceId=<hash(machineId,userId)> Version=...` on every request; 401 → re-auth; **no** `X-Emby-Token`, **no `api_key` query param — anywhere, including the WS handshake**. — `AuthHeaders` builds the one header (`Token` appended only when a token exists); the static test greps the module for both forbidden transports. 401 surfaces as `*Unauthorized` for the caller's re-auth policy (token refresh is SV-2's).
- [x] Port `POST /Users/AuthenticateByName` flow (username/password → `AccessToken`, `ServerId`, session info). — `JellyfinClient::authenticate`, with the stable per-user DeviceId trap covered by the "re-login loop" test.
- [x] Port `UdpServerDiscovery` (broadcast probe + response parse) + manual-URL fallback. — `Discovery`: the probe constant, lenient reply parsing (fixture-tested), broadcast collection with a listen window and cancellation; the manual fallback is the caller passing `baseUrl` directly.
- [ ] Optional: QuickConnect flow (koalapaw implements it — port if cheap, else defer).
- [x] Browse: port `musicAlbumsWindow` / `musicArtistsWindow` / A-to-Z letter-offsets (koalapaw `JellyfinGateway.kt:689-721` has the exact query shapes: `IncludeItemTypes`, `Recursive`, `SortBy`, paging). — `musicAlbumsWindow` and `musicArtistsWindow` share `atoZWindow` (one item type, SortName ascending, paging, user data); `letterOffsets` ports `atoZLetterOffsets` plus `letterOffsetsFrom` (first index per bucket wins, digits/symbols/blank → "#"). Both fixture-replayed.
- [x] Search: `/Items` queries with `SearchTerm`; map results to our `Track` model. — `JellyfinClient::search`: `searchTerm`, recursive, caller-chosen item types, mapped to `library::Track` (RunTimeTicks/10000 → ms; `Artists[0]`, falling back to `AlbumArtist`). Fixture-replayed.
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
