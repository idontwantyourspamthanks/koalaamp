# SC-1 · Scrobbling

- **Milestone:** M3 · **Est:** 3d · **Depends:** SV-2, AU-2 · **Status:** in progress — module complete, app wiring pending

## Story
Report listens to Last.fm and/or ListenBrainz from the client, with an anti-double-scrobble
switch for users also running the server-side plugins. Server-side `/Sessions/Playing*`
reporting already shipped (SV-2); this is the independent scrobble path.

## Tasks
- [x] Last.fm desktop auth: API-key + shared-secret signature (MD5 of sorted params),
      `auth.getToken` → browser confirm → `auth.getSession` → session key.
- [x] Last.fm submission: `track.updateNowPlaying` on start, `track.scrobble` batched
      (artist/title/album/albumArtist/duration/trackNumber/timestamp), retry queue.
- [x] ListenBrainz: token auth (`Authorization: Token`), `playing_now` + `single` listens
      with `recording_mbid`/`artist_mbids`/`release_mbid` when available.
- [x] Scrobble policy: threshold (default 50% or 4 min), one listen per play, no
      re-scrobble after seek/restart; offline queue persisted and flushed.
- [ ] Anti-double-scrobble mode: per-server setting `Relay | Client | Off` (server
      plugin already scrobbles when set to Relay); UI copy explains the duplication.
- [x] Transport seam + fixtures: Last.fm and ListenBrainz request/response replay,
      signature verification, offline retry, policy matrix.

## Implementation notes
Module `src/scrobble/` (Qt-free): `Md5`, `LastFmClient` (api_sig, auth token/session,
now-playing, batched scrobble), `ListenBrainzClient` (Token header, playing_now/single,
MBIDs), `ScrobbleQueue` (atomic JSON, dedup keys, retry counts), `ScrobbleCoordinator`
(threshold/once-per-play/seek+restart dedup/modes), `SecretStore` seam, and a transport
seam with a libcurl adapter. Tests: `koalaamp_scrobble_tests`, 24 cases / 235 assertions.
Deferred: production wiring in `main.cpp`/UI (the module takes explicit calls), the
per-server Relay/Client/Off settings UI, browser launch for desktop auth, and the live
sandbox run.

## Acceptance
- Sandbox Last.fm: auth, now-playing, scrobble accepted; ListenBrainz token submit
  accepted. Signature pinned by a known-answer test.
- Killing the network mid-listen queues the scrobble and flushes it on reconnect.
- Policy tests: <50%/4 min → no scrobble; ≥ → exactly one; replay/seek does not
  duplicate; Relay mode suppresses the client submit.
- No credentials in logs or in any recorded fixture URL; tokens remain in the keychain.

## Out of scope
- Love/unlove sync beyond what Last.fm's track.love exposes (planned separately).
- Plex/Jellyfin server plugins themselves.
- Scrobble UI beyond the mode selector.

## References
- `spec.md` §8.4 — scrobbling contract
- Last.fm API 2.0 docs, ListenBrainz `submit-listens` docs
