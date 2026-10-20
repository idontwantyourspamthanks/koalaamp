# SV-3 · Plex client port (auth / browse / source)

- **Milestone:** M3 · **Est:** 3d · **Depends:** SV-1, LI-2 · **Status:** in-review — fixture-replayed; live PMS pass pending

## Story
The second server backend: plex.tv PIN/link sign-in, GDM discovery, music sections →
albums → tracks, search, artwork, and an `IServerSource` that SV-4's playback layer can
drive. Ported from koalapaw's field-tested `plex-api` module, with the conservative
version-gated posture the 2025–26 Plex churn record demands.

## Tasks
- [x] PIN/link auth (`PlexAuth`): `POST /api/v2/pins` (short 4-char code, identity
      headers only), user confirms at `https://plex.tv/link`, poll
      `GET /api/v2/pins/{id}` for `authToken`, then `GET /api/v2/user` for the account.
- [x] Resources: `GET /api/v2/resources?includeHttps=1&includeRelay=1` with the account
      token; keep server-capable resources with a PMS `accessToken`; choose LAN → direct →
      relay connection order.
- [x] GDM discovery: broadcast `M-SEARCH * HTTP/1.1` to `255.255.255.255:32414` and
      `239.0.0.250:32414`; parse `Name`, `Port` (default 32400), `Resource-Identifier`
      (= machineIdentifier) from the reply, answer host = source address.
- [x] PMS client: `/identity`, `/library/sections` (music sections are `type=artist`),
      albums via `/library/sections/{id}/all?type=9` with
      `X-Plex-Container-Start/Size`, album tracks via
      `/library/metadata/{ratingKey}/children`, search via `/hubs/search`, artwork via
      `X-Plex-Token` headers (no token query), rating via `PUT /:/rate`.
- [x] `PlexSource : IServerSource`: search, `openStream` over the part URL
      (`Media[0].Part[0].key`) with Range, `setRating`, `isAuthenticated`/`signOut`.
- [x] Offline fixture replay: PIN pending/complete, profile, resources, identity,
      sections, albums, album children, search, rating, stream 200/206.
- [ ] Live PMS container pass (auth incl. 2FA link, browse, art) — needs a PMS fixture
      container; the media-server runtime is not in this repo yet.

## Acceptance
- PIN flow: create → poll pending → poll complete → account + resources; the account
  token is exchanged for each server's resource token, never assumed to work on the PMS.
- Browse: one music section → albums → tracks; search returns tracks from `/hubs/search`;
  every request carries `X-Plex-Client-Identifier`, product/version/device/platform
  headers, and `Accept: application/json`; the token is a header (image/stream included
  where the transport can carry it).
- Mapping: `ratingKey` → id, `grandparentTitle` → artist, `parentTitle` → album,
  `index`/`parentIndex` → track/disc, `Media[0].Part[0].key` → stream path,
  `container`/`audioCodec` → codec, bitrate in bits/s.
- Rating: 4 stars → 8, 0 stars → −1 (unset), identifier `com.plexapp.plugins.library`.
- Stream: Range seek re-requests; body bytes reach the caller; 401/404 map to a clean
  failure.
- No token appears in any browsed/fixture URL (grep over fixtures + requests).

## Out of scope
- Playback/timeline/scrobble/watched — SV-4.
- Home users/kid switching, QuickConnect analogue, companion receiver, sonic analysis,
  transcoding (SV-4 decision handshake only).
- Multi-server simultaneous sessions; Jellyfin changes.

## References
- `spec.md` §8.3 — Plex contract + churn caveats
- `docs/research/servers.md` §2.3-2.6, §2.10
- koalapaw `plex-api/src/main/kotlin/com/koalapaw/plex/client/`:
  `PlexAuth.kt` (pins/resources/profile), `PlexClient.kt` (headers/put), `PlexBrowse.kt`
  (sections/windows/search), `GdmServerDiscovery.kt`, `PlexModels.kt`
