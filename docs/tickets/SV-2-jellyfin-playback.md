# SV-2 · Jellyfin playback + progress + remote control

- **Milestone:** M3 · **Est:** 3d · **Depends:** SV-1, AU-1 · **Status:** unstarted

## Story
The full server playback loop: pick a track on Jellyfin, stream it with header auth through our DSP, report position so the server dashboard and scrobble plugins see reality, and accept remote-control commands from other Jellyfin clients (play/pause/next/seek) as if we were an official app.

## Tasks
- [ ] Direct-play stream: `GET /Audio/{id}/stream?static=true` via our libcurl layer with `Authorization: MediaBrowser Token=...` (no token in URL), Range-capable, fed to libvlc via `libvlc_media_new_callbacks` (no temp files). — The byte stream landed: `JellyfinStream` (`src/servers/jellyfin/JellyfinStream.{hpp,cpp}`) is a `std::streambuf` over the static endpoint — a seek inside the buffered response moves the get pointer, a seek outside re-requests with `Range: bytes=N-`, and `Content-Range`/`Content-Length` are parsed for size and the real start. `openJellyfinStream` wraps it as the `std::istream` that `ITrackSource::openStream` wants; fixtures replay the 200 and the 206, and the test pins the URL, the missing query auth and the Range header. Open: the `libvlc_media_new_callbacks` adapter and a streaming `IHttpTransport` (the current seam returns a whole body per request).
- [ ] Transcode fallback: `/Audio/{id}/master.m3u8` (HLS) when the codec decision demands it; decision logic: direct unless unsupported-or-bandwidth-limited.
- [ ] Wire RG: read `NormalizationGain` fields via the SV-1 gateway into the AU-1 ReplayGain stage.
- [ ] Progress reporting: `POST /Sessions/Playing` (start), `/Sessions/Playing/Progress` (every 10s + on pause/unpause/seek/volume/track-change events, `EventName` set), `/Sessions/Playing/Stopped` (end) — `PlayMethod` correct per mode, `PlaySessionId` per queue session. — The gateway half landed: `JellyfinClient::reportPlayback(baseUrl, token, deviceId, PlaybackEvent{Start,Progress,Stop}, PlaybackReport)` posts the koalapaw payload shape (`ItemId`, `MediaSourceId`, `PlaySessionId`, `PlayMethod`, `PositionTicks`, `IsPaused`, `IsMuted`, `CanSeek`, `EventName`), token in the header only, fixture-replayed on all three endpoints. Open: the player wiring (10 s cadence, pause/seek hooks, `PlaySessionId` lifecycle).
- [ ] WS client for `/socket` (auth via header/first-frame handshake per koalapaw's AuthInterceptor pattern — **no api_key query**): handle `Play` (PlayNow/PlayNext/PlayLast), `Playstate` (Stop/Pause/Unpause/NextTrack/PreviousTrack/Seek), `GeneralCommand` (SetVolume, Seek, PlayPause), `KeepAlive` reply loop on `ForceKeepAlive`.
- [ ] Remote control maps onto the TrackScheduler queue (server says "play next" → enqueue, not hijack).
- [ ] E2E test against the SV-1 containers + fixtures for the WS message catalog.

## Acceptance
- E2E: play/pause/seek a track; a second client's session dashboard shows our position within ±5s.
- A cast from another Jellyfin client (or fixture-replay of `Play`/`Playstate` frames) controls us: play/pause/next/seek all work.
- Transcode fallback triggers when a deliberately-unsupported codec is forced (fixture), and `PlayMethod` reflects it.
- WS auth carries the token in headers, never the URL (grep assertion over recorded handshakes).
- 30-min stream session: no buffer stalls on LAN, no socket leaks.

## Out of scope
- Plex (SV-3/4). Scrobble dedup logic (SC-1 — we emit events, SC-1 decides where they go).
- SyncPlay (server-grouped playback) — not in v1.
- Server-side playlist management (Phase 2.5).

## References
- `spec.md` §8.2 — the full contract list
- koalapaw: playback reporting models + `PlaybackGateway` seam (`jellyfin-api/client/`); WS/auth patterns from `AuthInterceptor.kt`
- `docs/research/servers.md` §1.3-1.4 — endpoints, 10s cadence, PlayMethod values, WS message catalog (`/socket`, not Emby's `/embywebsocket`)
- `docs/tickets/FE-5-koalapaw-port-skeleton.md` — the stream plumbing base

## Notes / risks
- `PlayMethod` correctness matters: it feeds server-side scrobble plugins; reporting DirectStream when we direct-play breaks their accounting.
- WS reconnect: server restarts push `ServerRestarting` — reconnect with backoff and re-report the current session.
