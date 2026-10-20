# SV-4 · Plex playback + progress + ratings

- **Milestone:** M3 · **Est:** 2d · **Depends:** SV-3, AU-1 · **Status:** in-review — live PMS playback confirmed 2026-10-20; dashboard/scrobble evidence pending

## Story
Plex playback closes the loop: direct-play a track from the PMS with the right headers, keep the server's dashboard truthful via `/:/timeline`, and let ratings/watched state written by our skinned UI appear in Plex web. Deliberately conservative posture — Plex churns its API, so everything here is version-gated and fixture-pinned.

## Tasks
- [x] Direct play: `Media[0].Part[0].key` URL + `X-Plex-Token` header (token header, not URL param where avoidable), Range seek support, fed to libvlc via callbacks (same plumbing as SV-2).
- [x] Client-profile headers on metadata requests: `X-Plex-Client-Profile-Extra` advertising our direct-play codec set (flac/mp3/aac/alac/vorbis/opus/wav per libvlc) so PMS decisions match reality; `X-Plex-Platform=Generic`, `X-Plex-Client-Identifier` on everything.
- [x] Progress: `POST /:/timeline` (koalapaw `PlexPlayback` shape — `ratingKey`, `key`, `state` playing|paused|stopped, `time` ms, `duration` ms, signed-in-profile token) on start/interval/stop.
- [x] Ratings: `PUT /:/rate` with `key`, `identifier` (koalapaw `PLEX_LIBRARY_IDENTIFIER`), `rating` (favorite=10, unset=−1 — "verified live" in koalapaw).
- [x] Watched: `/:/scrobble` + `/:/unscrobble` — **port koalapaw's GET-on-ambient-token behavior verbatim** (koalapaw's own P3 note at `PlexDetail.kt:402` flags the verb as unverified live), then **pin the verb with a live smoke test in this ticket** and record the outcome in the ticket file + spec.
- [ ] Transcode path: gated OFF by default in v1 (documented); only the decision handshake is wired.
- [ ] Version-gate: feature-detect PMS version; friendly error surface if an endpoint shape changed (isolated to the plex module).

## Implementation notes
`PlexMediaSource` implements the engine's `IMediaStreamSource` over `PlexStream`
(Range seeks re-request); `libvlc` decodes a PMS part end-to-end in the gated
`plex_stream_libvlc_tests`. `makePlexMedia()` builds the `MediaRef` with the
server identity the reporter needs. `PlexTimeline` posts the koalapaw query shape
(`ratingKey`, `key=/library/metadata/{id}`, `state`, `time`, `duration`) with the
token header; `PlexPlaybackReporter` queues start/pause/unpause/stop and a 10 s
heartbeat, drained from `tick()`. Watched uses the GET behaviour with a P3 note.
PMS requests now carry `X-Plex-Client-Profile-Name/Extra` (direct-play music
profiles) and `plexVersionAtLeast()` is available; endpoint version-gating and
the transcode decision handshake are deliberately deferred (direct-only v1).

## Acceptance
- E2E vs PMS 1.4x container: play with seek (position visible in Plex dashboard within ±5s); rate a track 4-star → visible in Plex web; mark played → watch-state flips.
- The scrobble verb is pinned by live evidence (request logged, watch-state observed) and documented — replacing koalapaw's P3 note.
- Regression suite: fixture-replay of every recorded interaction (metadata, timeline, rate, scrobble) offline.
- No token appears in any recorded URL (grep over fixture files).

## Out of scope
- Plexamp-only features (sonic analysis, mixes) — no public API.
- Companion receiver mode (being *cast to*) — moving surface, skip.
- Transcode playback beyond the decision handshake.
- Last.fm/ListenBrainz (SC-1).

## References
- `spec.md` §8.3 — contract + the scrobble-verb caveat
- koalapaw: `plex-api/.../client/{PlexPlayback,PlexDetail,PlexClient}.kt` — timeline POST shape (`PlexPlayback.kt:415-431`), rate (`PlexDetail.kt:132-155`), scrobble GET + P3 note (`PlexDetail.kt:402-419`), put-helper (`PlexClient.kt:189`)
- `docs/research/servers.md` §2.3-2.6, §2.10 — header table, timeline/scrobble contracts, the 2025-26 churn record that justifies the conservative posture

## Notes / risks
- If the live smoke shows PUT is required for scrobble (contradicting koalapaw's GET), just pin PUT and note it — the point is evidence over either of our guesses.
