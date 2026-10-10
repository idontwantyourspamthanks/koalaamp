# Plex + Jellyfin Client Integration Research (for a third-party Winamp-like music player)

Scope: clean-room, Winamp 2/5-skin-compatible Avalonia (.NET) music player with local libraries + Plex + Jellyfin integration. This report covers only the server-integration surface. State of the art as of 2026-10-10; every load-bearing claim is cited; uncertain items are marked `[UNVERIFIED]`.

---

## 1. Jellyfin

### 1.1 Base URL, API style, discovery
- Default base URL `http://localhost:8096`; API is ASP.NET Core/Kestrel REST, Swagger/OpenAPI 3.0, JSON primary + XML supported. Swagger UI at `http://<host>/api-docs/swagger/index.html` (ReDoc at `.../redoc/index.html`). Controllers live under `/Audio`, `/Items`, `/Sessions`, `/Users`, `/Playstate`, etc. — https://jellyfin-jellyfin.mintlify.app/development/api-overview
- Server is open-source GPL-2.0 (https://github.com/jellyfin/jellyfin, 57.6k★, README requires .NET 10 SDK to build as of master).

### 1.2 Authentication (IMPORTANT — the brief's premise is outdated)
- Authenticate username/password: `POST /Users/AuthenticateByName`, body `{"Username":"...","Pw":"..."}`, with a **client-info** Authorization header:
  `Authorization: MediaBrowser Client="MyApp", Device="MyDevice", DeviceId="unique-id", Version="1.0.0"`.
  Response returns `User`, `SessionInfo`, `AccessToken`, `ServerId`. — https://jellyfin-jellyfin.mintlify.app/api/authentication/overview
- Authenticated requests then send `Authorization: MediaBrowser Token="<token>", Client="...", Device="...", DeviceId="...", Version="..."`. The `ApiKey` query param exists but is discouraged (leaks into proxy logs). API keys are admin-level and persistent. — author of the Jellyfin SDK, https://gist.github.com/nielsvanvelzen/ea047d9028f676185832e51ffaf12a6f
- **`X-Emby-Token` / `X-MMediaBrowser-Token` / `X-Emby-Authorization` / `api_key` query are deprecated.** An opt-out was added in 10.11 (PR #13306), then **disabled by default in 10.12** (PR #15559, merged 2025-11-27), and they are **removed entirely in Jellyfin 12.0**. Clients relying on them "are not supported" in 12.0. — https://gist.github.com/nielsvanvelzen/ea047d9028f676185832e51ffaf12a6f , https://github.com/jellyfin/jellyfin/pull/15559
- Device identifiers: the server allows only **one access token per `DeviceId`**, so a multi-user client must derive a per-user `DeviceId` (e.g. hash of device id + username). — same gist.
- Realm of pain: PR #15559 explicitly broke Infuse, Manet and Fintunes; maintainers say this is expected for clients not following best practice — https://github.com/jellyfin/jellyfin/pull/15559 . This is the single biggest compatibility risk for a new client.
- TLS: Jellyfin has no built-in certificate UI in the main config (only `hostwebclient`, FFmpeg probesize, PublishedServerUrl, DB tuning) — TLS is done at Kestrel (`--certfile`/pfx) or, more commonly, a reverse proxy (Caddy recommended, Nginx/Traefik/HAProxy/Apache) with Let's Encrypt. **Must set "Known Proxies"** and **must allow WebSockets**. — https://jellyfin.org/docs/general/administration/configuration/ , https://jellyfin.org/docs/general/post-install/networking/reverse-proxy/

### 1.3 Audio playback endpoints
The `AudioController` (`Jellyfin.Api/Controllers/AudioController.cs`, master) exposes:
- `GET|HEAD /Audio/{itemId}/stream`
- `GET|HEAD /Audio/{itemId}/stream.{container}`

Key query params (all optional): `static`, `container`, `audioCodec`, `audioBitRate`, `audioSampleRate`, `maxAudioBitDepth`, `audioChannels`, `maxAudioChannels`, `transcodingMaxAudioChannels`, `audioStreamIndex`, `startTimeTicks` (1 tick = 10,000 ms per the XML doc comment; i.e. 10^7 ticks/sec), `copyTimestamps`, `enableAutoStreamCopy`, `allowAudioStreamCopy`, `context` (`EncodingContext`), `playSessionId`, `deviceId`, `segmentContainer`, `segmentLength`, `minSegments`, `maxStreamingBitrate`, `enableAudioVbrEncoding`. — https://github.com/jellyfin/jellyfin/blob/master/Jellyfin.Api/Controllers/AudioController.cs
- **Direct play (no server encoding):** `?static=true` streams the original file; use no extension or the original extension. — same source + https://typescript-sdk.jellyfin.org/interfaces/generated-client.AudioApiGetAudioStreamByContainerRequest.html
- **HLS audio:** `GET /Audio/{itemId}/master.m3u8` (master playlist) with `audioCodec`∈{aac,mp3,opus}, `audioBitRate`, `maxStreamingBitrate`, `maxAudioChannels`, `segmentContainer`/`segmentLength`. Video HLS is `/Videos/{id}/master.m3u8` + `/live.m3u8` + `/main.m3u8`. — https://jellyfin-jellyfin.mintlify.app/api/playback/hls
- The `hls1.m3u8` naming in the brief is legacy **Emby** convention; Jellyfin uses master/variant (`master.m3u8`, `main.m3u8`). Recent work hides HLS controllers and deprecates unused endpoints (PR #16715) — treat HLS endpoints as evolving. — https://github.com/jellyfin/jellyfin/pull/16715
- **Transcode decision is client-driven:** the client sends a DeviceProfile (codecs, resolution, bitrate, constraints); the server picks the best output within those constraints. There is no manual resolution override. Playback modes: Direct Play, Remux, Direct Stream, Transcode. — https://jellyfin.org/docs/general/server/transcoding/

### 1.4 Progress / scrobble / remote control
- `POST /Sessions/Playing` (start), `POST /Sessions/Playing/Progress` (**every 10 s** and immediately after any user interaction: pause/unpause/seek/volume/repeat/track change), `POST /Sessions/Playing/Stopped`. Body: `QueueableMediaTypes`, `CanSeek`, `ItemId` or `Item`, `MediaSourceId`, `AudioStreamIndex`, `IsPaused`, `IsMuted`, `PositionTicks`, `VolumeLevel`, `PlayMethod`∈{Transcode,DirectStream,DirectPlay}, `PlaySessionId`, `PlaylistIndex/Length`, `PlaybackRate`, plus `EventName` on Progress (TimeUpdate/Pause/Unpause/VolumeChange/RepeatModeChange/AudioTrackChange/…) — https://dev.emby.media/doc/restapi/Playback-Check-ins.html (Emby docs; Jellyfin is the same API lineage and this remains the documented contract)
- WebSocket alternative: send `{MessageType:"ReportPlaybackProgress", Data:{...}}` (also `ReportPlaybackStart`/`Stop`). — same source.
- Mark played/unplayed: `POST|DELETE /Users/{userId}/PlayedItems/{id}`. Ratings/favorites/played state via `/Users/{userId}/Items/{id}/UserData`. — https://dev.emby.media/doc/restapi/Playback-Check-ins.html , https://jellyfin-jellyfin.mintlify.app/development/api-overview
- **WebSocket URL:** `ws(s)://<host>/socket?api_key=<token>&device_id=<deviceId>`. Jellyfin uses **`/socket`**, not Emby's `/embywebsocket`. send/recv frames are `{MessageType, Data}`; inbound types include `GeneralCommand`, `Play`, `Playstate`, `UserDataChanged`, `KeepAlive`/`ForceKeepAlive`, `ServerRestarting`, etc. `ForceKeepAlive` requires replying `KeepAlive`. — https://github.com/jellyfin/jellyfin-apiclient-python/blob/b6561909/jellyfin_apiclient_python/ws_client.py , https://github.com/MediaBrowser/Emby/wiki/Web-Socket (message catalog)
- Remote control: the server pushes `Play` (ItemIds, PlayCommand PlayNow/PlayNext/PlayLast, StartPositionTicks, …) and `Playstate` (Stop/Pause/Unpause/NextTrack/PreviousTrack/Seek) plus `GeneralCommand` entries. A third-party player should implement these to be controllable. — https://github.com/MediaBrowser/Emby/wiki/Web-Socket

### 1.5 Rate limits, SSL
- **No built-in API/login rate limiting.** Issue #13043 ("No rate Limit on login Screen") was closed **not planned**; maintainers say "This is a job for fail2ban." A later commenter disputed but it stayed closed. So JellyAmp should not expect server throttling; reverse-proxy rate limits are the norm. — https://github.com/jellyfin/jellyfin/issues/13043 , docs recommending proxy rate limits: https://jellyfin.org/docs/general/post-install/networking/reverse-proxy/
- SSL: see 1.2 — reverse proxy + Let's Encrypt; must configure Known Proxies and WebSockets.

### 1.6 Gapless
- The official **web client is not gapless** historically (jellyfin-web #1132, 2020, closed as enhancement; #5022 buffers between songs). There is a live 10.11 regression where browser HLS audio playback times out when transcoding is required (#7546). — https://github.com/jellyfin-web/jellyfin-web/issues/1132 , https://github.com/jellyfin-web/jellyfin-web/issues/5022 , https://github.com/jellyfin-web/jellyfin-web/issues/7546
- **There is no server-side gapless API.** Gapless must be implemented client-side (pre-buffer the next track and cross the boundary in the audio pipeline). Official/3rd-party native clients do this themselves (e.g. Jellyfin Android issue #981; Finamp). — https://github.com/jellyfin/jellyfin-android/issues/981
- Full-file `?static=true` direct play is the best substrate for gapless on Jellyfin (no re-encode boundary artifacts).

### 1.7 Scrobbling / MusicBrainz plugin compatibility (server-side)
- Jellyfin plugins subscribe to server playback events (`PlaybackStart`, `PlaybackStopped`, `UserDataSaved`). A third-party client should therefore emit correct `/Sessions/Playing*` reports and UserData changes (rating/love/played), which is what triggers server-side scrobbling.
- **Last.fm plugin** (fork of the Emby plugin): `jesseward/jellyfin-plugin-lastfm` (+ active fork `danielfariati/jellyfin-plugin-lastfm`). README states it "cannot be distributed with Jellyfin due to a missing compatible license." — https://github.com/jesseward/jellyfin-plugin-lastfm
- **ListenBrainz plugin** (`lyarenei/jellyfin-plugin-listenbrainz`) is mature and feature-rich:
  - sends `playing_now` on `PlaybackStart`; sends a real listen when playback ≥4 min or ≥50% of runtime (optionally requiring a recording MBID in strict mode);
  - listen cache + resubmit task every 24–25 h; MusicBrainz metadata enrichment;
  - two-way favorite/love sync; generated-playlist sync (Weekly Jams, Weekly Exploration, Top Discoveries, Top Missed);
  - track matching ladder MBID → album MBID+title → related recordings → artist+title → album+title.
  — https://github.com/lyarenei/jellyfin-plugin-listenbrainz/blob/main/doc/how-it-works.md
- Implication: if JellyAmp scrobbles directly *and* the server plugin is enabled, listens will duplicate. Recommend a per-user toggle (client-side direct scrobble OR rely on server plugin).

### 1.8 Official client SDKs
- **Official .NET SDK: `Jellyfin.Sdk`** — NuGet **2026.10.10**, **MIT**, target frameworks net10.0 / net8.0 / netstandard2.0 / 2.1, generated with **Microsoft Kiota** from the server OpenAPI (deps Microsoft.Kiota.* 2.1.2). Repo `jellyfin/jellyfin-sdk-csharp`, README literally says "Documentation WIP." Published stable releases track server releases (same-day 2026.10.10). — https://www.nuget.org/packages/Jellyfin.Sdk , https://github.com/jellyfin/jellyfin-sdk-csharp
- Other official SDKs: TypeScript `@jellyfin/sdk` (https://typescript-sdk.jellyfin.org/), Kotlin `jellyfin-sdk-kotlin`. Older unofficial/hand-written: `jellyfin-apiclient-python` (PyPI 1.18.0) and archived `jellyfin-apiclient-javascript` (https://github.com/jellyfin-archive/jellyfin-apiclient-javascript).
- **Coverage of audio playback endpoints:** because the SDK is generated from the full OpenAPI, the AudioController operations exist. However streaming endpoints return binary `File` results; Kiota's generated request-builder for a binary body is awkward, and the SDK does not hand-hold HLS/direct-play URL construction or DeviceProfile negotiation. **[INFERENCE]** Practical approach: use `Jellyfin.Sdk` for auth/library/UserData/session APIs and build the `/Audio/{id}/stream?...` or `/Audio/{id}/master.m3u8?...` URLs yourself (the stream URL is trivial and stable).
- No NSwag-era .NET SDK remains current; Kiota is the generation tool. (The brief's "NSwag" guess is superseded.) — README + NuGet deps above.

---

## 2. Plex

### 2.1 API docs status
- For the first time Plex published **official PMS API docs at https://developer.plex.tv** (Sept 22, 2025, "Plex Pro Week '25: API Unlocked"). It covers auth, library/playlists/collections/activities, and includes the new JWT auth. Community OpenAPI spec: `LukeHagar/plex-api-spec`. — https://www.plex.tv/blog/plex-pro-week-25-api-unlocked/ , https://github.com/LukeHagar/plex-api-spec , https://developer.plex.tv/pms/
- All PMS endpoints default to **XML**; send `Accept: application/json` for JSON. — https://github.com/GLinnik21/plx-native/blob/main/docs/plex-api.md , https://www.nuget.org/packages/LukeHagar.PlexAPI.SDK

### 2.2 Authentication
**Legacy opaque-token PIN flow (still required for PMS playback in practice):**
1. Generate a stable `X-Plex-Client-Identifier` (UUID, per install).
2. `POST https://plex.tv/api/v2/pins` (or `/api/v2/pins`) with that header; omit `strong=true` for a short 4-char code the user types at `https://plex.tv/link`, use `strong=true` for a long opaque code used in a headless URL. Returns `{id, code}`.
3. User authenticates at `https://app.plex.tv/auth#?clientID=<id>&code=<code>&context%5Bdevice%5D%5Bproduct%5D=<app>`.
4. Poll `GET https://plex.tv/api/v2/pins/{id}` → `authToken` (the user access token).
5. `GET https://clients.plex.tv/api/v2/resources?includeHttps=1&includeRelay=1&includeIPv6=1` → PMS instances with connection URLs + `accessToken` per resource. Prefer `local`, relay last (bandwidth-limited).
— https://github.com/iobroker-community-adapters/ioBroker.plex/blob/master/docs/plex-api-auth.md , https://www.nuget.org/packages/LukeHagar.PlexAPI.SDK (OAuth PIN flow summary)

**JWT authentication (new, Sept 2025):** per-device Ed25519 JWK registration; tokens expire after **7 days**; refresh via `GET /api/v2/auth/nonce` (nonce valid 5 min) → signed device JWT (`aud: plex.tv`, `iss: <clientIdentifier>`) → `POST /api/v2/auth/token` → new token. Use the JWT exactly like a legacy token in `X-Plex-Token`. Error codes: 498 expired, 422 signature/thumbprint, 400 malformed, 429 nonce rate limit. — https://www.plex.tv/blog/plex-pro-week-25-api-unlocked/ , https://github.com/iobroker-community-adapters/ioBroker.plex/blob/master/docs/plex-api-auth.md

**Critical JWT gotcha (2025–2026):** PMS only understands **legacy** tokens. Calling `/api/v2/resources` with a JWT returns a **JWT** for the PMS, which the server rejects with 401. `/devices.xml` stopped returning the server token. Developer-discovered workaround (Aug 2026): use **`/api/v2/devices`, which always returns a legacy token**. Net: implement PIN/legacy-token flow for PMS; treat JWT as plex.tv-only until Plex documents a PMS handoff. — https://forums.plex.tv/t/question-on-https-clients-plex-tv-api-v2-resources-and-jwt-authentication/934478

### 2.3 Required headers
| Header | Notes |
|---|---|
| `X-Plex-Token` | required on every PMS request; may also be a query param |
| `X-Plex-Client-Identifier` | stable per-install UUID; **required** for transcode sessions + `/:/timeline` |
| `X-Plex-Product`, `X-Plex-Version`, `X-Plex-Device-Name` | identify client in sessions/dashboard |
| `X-Plex-Platform` | **required with a recognized value (e.g. `Generic`) to unlock `/video/:/transcode/universal/*`**, else 400 |
| `X-Plex-Session-Identifier` | required for the universal transcode handshake |
All `X-Plex-*` headers may also be sent as query args. — https://github.com/GLinnik21/plx-native/blob/main/docs/plex-api.md , https://github.com/iobroker-community-adapters/ioBroker.plex/blob/master/docs/plex-api-auth.md

### 2.4 Browsing music libraries
- `GET /library/sections` → `Directory[]` with `key` (section id), `type`, `title`; music sections have their own type tag. — https://github.com/GLinnik21/plx-native/blob/main/docs/plex-api.md
- Per library: `GET /library/sections/{id}/all` (flat list of Metadata), filters `genre=`, `year=`, `unwatched=1` etc., pagination via `X-Plex-Container-Start`/`X-Plex-Container-Size`, `totalSize` only when paginating. — same source
- Item hierarchy: `GET /library/metadata/{ratingKey}` (full fields incl. `duration` ms, `viewOffset` ms resume point, `Media[].Part[].key`, `Media[].Part[].Stream[]` codecs); children via `.../children` and `.../allLeaves`; hubs via `GET /hubs` and `/hubs/sections/{id}`. — same source
- **Music type codes** (Artist/Album/Track) are **not enumerated in the verified plx-native doc** (which lists Movie=1, Show=2, Season=3, Episode=4 only). `[UNVERIFIED]` — the Python PlexAPI `Audio` class exposes `guid`, `index`, `key` for artist/album/track (https://python-plexapi.readthedocs.io/en/latest/modules/audio.html); resolve numeric codes live from `/library/sections/{id}/all` rather than hardcoding.
- Artwork: `GET /photo/:/transcode?width=&height=&url=<url-encoded thumb>` for sized JPEGs; raw `{thumb}?X-Plex-Token=` also works. — plx-native

### 2.5 Playback (direct play vs server transcode)
- **Decision is PMS-side via client profiles.** Request metadata with `X-Plex-Client-Profile-Extra` describing direct-play and transcode targets (including `type=musicProfile&protocol=http&container=flac&audioCodec=flac`, `musicProfile…container=mp4&audioCodec=alac`, `add-transcode-target(type=musicProfile&context=streaming&protocol=hls&container=mpegts&audioCodec=aac)`). A real third-party player's profile string is shown in https://forums.plex.tv/t/bug-transcoded-hls-fails-to-stream/932371 .
- **Direct play (music):** fetch `Media[0].Part[0].key` = `/library/parts/{id}/{timestamp}/{file}` directly with `X-Plex-Token`; supports byte-Range seek. This is the preferred path since PMS does not re-encode. — plx-native (video equivalent); music uses the same Part model. `[UNVERIFIED]` exact music `/library/parts` acceptance of `download=1` vs plain stream, but the Part URL is the standard direct-play mechanism.
- **Transcode (audio):** Plex transcodes audio (commonly to Opus/AAC/MP3) when the client profile says it can't direct-play. Evidence of widespread Plex audio→Opus/AAC transcoding: https://forums.plex.tv/t/plexamp-transcoding-files-to-opus-even-at-maximum-settings/852912 , https://forums.plex.tv/t/plex-web-is-transcoding-music-files-opus-aac-is-it-a-bug/856947 . The exact music-universal URL path is not in the verified doc set — `[UNVERIFIED]`; video uses `/video/:/transcode/universal/decision` then `start.mkv`, and music mirrors it under `/music/:/transcode/universal/...` per convention only.
- Progressive transcode recipe (verified for video, architecture reusable): handshake `decision` first, then `start.mkv`; requires `X-Plex-Session-Identifier`; reply is chunked (must de-chunk); stop with `/video/:/transcode/universal/stop?session=`. — plx-native (2026-07 device verification).
- Hardening: `X-Plex-Token` in URLs still works (support article still documents it) though it no longer appears automatically in the web UI URL — https://forums.plex.tv/t/x-plex-token-no-longer-appears-in-the-url-per-the-documentation/931301 .

### 2.6 Progress, scrobble, ratings, playlists
- Progress/resume (undocumented legacy but universally used): `GET /:/timeline?ratingKey={rk}&key=/library/metadata/{rk}&state=playing|paused|stopped&time={ms}&duration={ms}&X-Plex-Client-Identifier={uuid}` → PMS stores `viewOffset`. — plx-native
- Mark watched: `PUT /:/scrobble?key={rk}&identifier=com.plexapp.plugins.library`; undo via `/:/unscrobble`. — plx-native
- Rating: `PUT /:/rate?key={rk}&rating={0-10}&identifier=com.plexapp.plugins.library` (legacy; official docs cover library/playlists). — https://developer.plex.tv/pms/ (library section)
- Playlists via official API (create/update/add items), demonstrated in the Plex blog screenshot. — https://www.plex.tv/blog/plex-pro-week-25-api-unlocked/

### 2.7 Music analysis: loudness + sonic
- Server setting **"Analyze audio tracks for loudness"** (never / scheduled / scheduled+on-add). The results are stored in the Plex DB, **not written to files**, and exposed in item metadata (e.g. a `loudness` value and `musicAnalysisVersion`). — https://support.plex.tv/articles/200289526-library/ , https://www.plex.tv/blog/plex-pro-week-25-api-unlocked/ (analysis storage), https://python-plexapi.readthedocs.io/en/latest/modules/audio.html (`musicAnalysisVersion` field)
- No reliable separate album-vs-track gain: Symfonium's developer observed album gain and track gain are the same single value; users derived approximately `ReplayGain ≈ -(loudness) − 18`. So a third-party player can read `loudness` and apply its own gain, but cannot expect Plex's exact leveling curve. — https://support.symfonium.app/t/loudness-normalisation-for-plex/9016
- **Sonic analysis** (Plex Pass, x86/x86-64 only, neural net, weeks of CPU): enables sonically-similar artists/albums/tracks, Track/Album Radio, Mixes For You — but **"currently only available when using Plexamp for playback."** No public sonic-data API for third parties. — https://support.plex.tv/articles/sonic-analysis-music/
- The brief's phrasing "adds replay gain + bpm tags" is inaccurate: Plex stores loudness **outside** the files and does not write BPM tags. `[UNVERIFIED]` for any BPM behavior.

### 2.8 Remote control / Plex Companion
- "Plex Companion" is the remote-control/fling protocol; a receiver app appears under `GET /clients` on PMS and must enable remote control in its settings. Not officially specified publicly. — https://support.plex.tv/articles/201812803-plex-companion-remote-control-issues/
- Control surface (community-documented): the receiver runs an HTTP server; controllers call `/player/playback/playMedia`, `/play`, `/pause`, `/stop`, `/stepForward`, `/stepBack`, `/seekTo`, `/setVolume`, `/setAudioStream`, `/setSubtitleStream`; discovery via GDM (UDP 32410/32412/32413/32414). `[UNVERIFIED]` as an authoritative spec (community docs: https://python-plexapi.readthedocs.io/en/latest/modules/client.html ; companion v1 wiki https://github.com/ytmdesktop/ytmdesktop/wiki ).
- Plex for Mobile apps **2025.10+** changed companion behavior (support article now says it "does not apply" to them), so this surface is actively moving. — https://support.plex.tv/articles/201812803-plex-companion-remote-control-issues/

### 2.9 Third-party music clients today
- **Plexamp** — official, proprietary/closed. Features: loudness leveling, true gapless, "Sweet Fades", configurable preamp, 7-band EQ. Bundles MPD (GPLv2), FFTW (GPLv2), Electron (MIT), libsoundio (MIT) — see Plex's own license page. — https://www.plex.tv/plexamp/ , https://support.plex.tv/articles/204096476-license-information/
- **Symfonium** (Android, paid) — Plex + Jellyfin + Emby + Kodi + Subsonic/OpenSubsonic; has documented provider feature matrix. — https://support.symfonium.app/t/supported-features-by-media-providers/1178
- **Feishin** (Electron, GPL-3.0, by jeffvli) — supports Navidrome/Jellyfin/OpenSubsonic. **Plex is not natively supported**; users must use the `lux032/feishin` fork — strong evidence that Plex is materially harder to integrate than Jellyfin. — https://github.com/jeffvli/feishin , https://github.com/lux032/feishin (referenced in README) , Plex PR: https://github.com/jeffvli/feishin/pull/1861
- **Sonixd** — predecessor to Feishin (deprecated). **plezy** — Plex Android TV. **Mistglow** — Plex cast target issues. `[UNVERIFIED]` for other minor clients.
- MusicBee: no real Plex plugin — only user-written playlist-sync scripts. `[UNVERIFIED]` — https://gist.github.com/yarnairb/d905903a550da571d9346aff3a6d3fdb

### 2.10 What Plex changed in 2023–2025 that broke third parties (cited forum threads)
1. **Sept 17, 2025 — Plex Web v4.152.0 OAuth breakage.** The login page began sending `Cross-Origin-Opener-Policy: same-origin`; third-party apps that polled `popup.closed` cancelled auth. Broke Tautulli, Overseerr, Jellyseerr, Ombi, Organizr. Plex apologised: "Breaking auth on 3rd party apps was not intentional." — https://forums.plex.tv/t/plex-oauth-authenticate-with-plex-broken-after-plex-web-update-v4-152-0/931098 (Topic 931098, 28 posts, 3.1k views)
2. **Oct 2025 — transcode regression.** A third-party player app hit `Unrecognized option 'segment_copyts'` / `Error splitting the argument list: Option not found` on Server 4.152.0 with a client-profile that worked before. Evidence Plex's transcoder/profile handling is not stable across server releases. — https://forums.plex.tv/t/bug-transcoded-hls-fails-to-stream/932371
3. **2025–2026 — JWT migration churn.** 7-day tokens, `/devices.xml` no longer returns the server token, `/api/v2/resources` returns JWT PMS rejects; developers stranded for months. — https://forums.plex.tv/t/question-on-https-clients-plex-tv-api-v2-resources-and-jwt-authentication/934478
4. **Dec 2025 / Jan 2026 — token-disclosure CVEs** (CVE-2025-69414, CVE-2025-69416) in PMS through 1.42.2.10156, enabling a non-server device token to retrieve unrelated access tokens — likely drove further auth hardening. — https://www.ipbuf.com/static/cve/2025/CVE-2025-69414_en.html , https://www.sentinelone.com/vulnerability-database/cve-2025-69416
5. Historical: plugin system discontinued 2018 (Plex is unlikely to add third-party extension points). — https://www.plex.tv/blog/plex-pro-week-25-api-unlocked/

Conclusion: Plex's API is officially documented now (a positive), but the last 18 months show repeated, sometimes-unannounced auth and transcoder breakages; any third-party Plex support must be treated as best-effort and version-gated.

---

## 3. Server-side features (Jellyfin vs Plex)

### 3.1 Transcoding limits
- **Plex:** transcoding (and especially hardware transcoding) is tied to Plex Pass; remote/relay streaming is bandwidth-limited; the server decides direct-play vs transcode from the client profile. Music transcodes to Opus/AAC/MP3 — Plex does support Opus output. Relay connections are throttled and should be last resort. — https://www.plex.tv/blog/plex-pro-week-25-api-unlocked/ (Pass-gated transcoding context), https://forums.plex.tv/t/plexamp-transcoding-files-to-opus-even-at-maximum-settings/852912 , https://github.com/iobroker-community-adapters/ioBroker.plex/blob/master/docs/plex-api-auth.md (relay bandwidth limit)
- **Jellyfin:** server picks settings from client DeviceProfile within its capabilities; HW accel on Intel/AMD/NVIDIA/Apple/Rockchip; 4 modes Direct Play/Remux/Direct Stream/Transcode; no manual resolution override. Audio codecs aac/mp3/opus supported. 10.10 upgraded FFmpeg to 7.0, added AC-4 and better downmixing. — https://jellyfin.org/docs/general/server/transcoding/ , https://jellyfin.org/posts/jellyfin-release-10.10.0/

### 3.2 Gapless
- Plex: Plexamp markets "true gapless playback" and "Sweet Fades", both **client-side**; no server gapless contract. — https://www.plex.tv/plexamp/
- Jellyfin: no server gapless; web client non-gapless; client must implement. See 1.6.

### 3.3 Loudness normalization server-side (state 2025–2026)
- **Plex:** server-side loudness analysis exists and is stored in the DB (`loudness`, `musicAnalysisVersion`), consumed by Plexamp for leveling; no track/album distinction, and no API to fetch a ready-made gain — clients read the raw `loudness` metadata. — https://support.plex.tv/articles/200289526-library/ , https://support.symfonium.app/t/loudness-normalisation-for-plex/9016 , https://python-plexapi.readthedocs.io/en/latest/modules/audio.html
- **Jellyfin:** no server-side music loudness normalisation as of 10.10/10.11. ReplayGain is a **client-side** feature: the server scans/serves ReplayGain tags (PR #11418, 2023; album-gain parsing advancing in 2026 PR #17315), and the web client applies gain via a WebAudio gain node when normalization is enabled (jellyfin-web PR #6142). A known bug: `NormalizationGain` ignored the Track/Album preference (jellyfin #14346). — https://github.com/jellyfin/jellyfin/pull/11418 , https://github.com/jellyfin/jellyfin/pull/17315 , https://github.com/jellyfin/jellyfin-web/pull/6142 , https://github.com/jellyfin/jellyfin/issues/14346
- Net for JellyAmp: read ReplayGain tags from Jellyfin metadata (or the file), read `loudness` from Plex metadata, and apply gain/preamp locally. No server does the DSP for you.

### 3.4 Rating / favorite / playlist sync
- Jellyfin: UserData (rating/favorite/played/play position) per user via `/Users/{userId}/Items/{id}/UserData`; playlists via Playlists API; SyncPlay for synchronized playback; ListenBrainz plugin syncs loved tracks both ways and imports generated playlists. — https://jellyfin-jellyfin.mintlify.app/development/api-overview , https://github.com/lyarenei/jellyfin-plugin-listenbrainz/blob/main/doc/how-it-works.md
- Plex: rating via `/:/rate`, progress via `/:/timeline`, watched via `/:/scrobble`; playlists via official playlists API. — plx-native , https://developer.plex.tv/pms/

---

## 4. Existing .NET client libraries

| Library | Version | License | Last published | Target | Verdict |
|---|---|---|---|---|---|
| `Jellyfin.Sdk` (official) | 2026.10.10 | MIT | 2026-10-10 (same day) | net8/net10/netstandard2.0-2.1 | **Use.** Kiota-generated, current, tracks server OpenAPI. |
| `LukeHagar.PlexAPI.SDK` | 0.21.0 | (package license page) | 2026-07-19 (0.20.5→0.21.0 in 2026) | net8.0 | **Best Plex .NET option.** Generated from LukeHagar/plex-api-spec; covers PMS + plex.tv v2; documents rate limiting/backoff. |
| `Plex.Api` (jensenkd) | 4.1.2 | MIT | **2022-10-18** | net6.0 | **Stale (~4 yrs); avoid as primary.** https://github.com/jensenkd/plex-api |
| `LukasParke/plexcsharp` | — | — | active-ish | .NET | Speakeasy-generated SDK with a Timeline SDK; `[UNVERIFIED]` maturity. https://github.com/LukasParke/plexcsharp |
| `plex-api` (cloudbitvps) | — | — | dead | — | Automatically exported from old code.google.com; obsolete. |

- No **official** Plex .NET SDK exists. No Avalonia-specific server bindings exist for either server; use these SDKs or plain `System.Net.Http` + `System.Text.Json` (Plex XML→JSON via `Accept: application/json`).
- SoundCloud/Emby lineage `Emby.ApiClient` is historical only; Jellyfin forked it and replaced it with the Kiota SDK.

### 4b. Last.fm / ListenBrainz scrobbling from a desktop client
- **Last.fm (desktop auth, no browser-embedded secrets):** get API key + shared secret; `auth.getToken` (signed) → open browser at `https://www.last.fm/api/auth/?api_key=..&token=..` → `auth.getSession` → **session key (infinite lifetime by default; user can revoke)**. Sign all calls: sort params alphabetically, concatenate `<name><value>`, append secret, md5 → `api_sig`. Scrobble via `track.scrobble` (supports batched timestamps), now-playing via `track.updateNowPlaying`, `track.love`/`track.unlove`. — https://www.last.fm/api/desktopauth , https://www.last.fm/api/show/track.scrobble , https://www.last.fm/api/authspec
- **ListenBrainz:** token from user settings; `POST https://api.listenbrainz.org/1/submit-listens` with `Authorization: Token <token>`, body `{listen_type: single|import|playing_now, payload:[{listened_at, track_metadata:{artist_name, track_name, release_name, additional_info:{recording_mbid,...}}}]}`. Auth headers grant relaxed rate limits. Metadata lookup `/1/metadata/lookup/`, feedback and history endpoints exist. — https://listenbrainz.readthedocs.io/en/latest/users/api-usage.html
- MusicBrainz IDs: use `ProviderIds` from Jellyfin metadata (MusicBrainzTrack/Album/Artist) so ListenBrainz matching and the strict-MBID listen path work well; for Plex there is no MBID guarantee in metadata. `[INFERENCE]` from plugin design (https://github.com/lyarenei/jellyfin-plugin-listenbrainz/blob/main/doc/how-it-works.md).
- **Avoid double-scrobbling** when the Jellyfin server-side Last.fm/ListenBrainz plugin is enabled (see 1.7); expose a mode selector.

---

## 5. Legal / licensing

### 5.1 Plex Terms of Service (rev. 2025-10-30)
Key clauses that bear on a third-party client (https://www.plex.tv/about/privacy-legal/plex-terms-of-service/):
- License is personal, non-commercial, revocable.
- Prohibited: (a) using Plex to "create any service, software or documentation that performs substantially the same functionality"; (b) "disassemble, decompile, reverse-engineer, or use any other means to attempt to discover any source code, algorithms, trade secrets, or applications"; (e) "disable, circumvent, or otherwise avoid or undermine any security device, mechanism, protocol, or procedure."
- "Interfacing Software" is broadly defined as any software that calls PMS, including client applications. You are "responsible and liable" for it, must ship a privacy notice and a copyright notice, and **grant Plex a worldwide, royalty-free license to use/promote/distribute the Interfacing Software** unless you notify Plex (with a ≥30-day transition period).
- Trademarks may not be used without express permission.
- Plex may add/modify/delete features at any time; continued use = acceptance.

**Practical reading:** Building a client that uses the *documented* API (developer.plex.tv) is the sanctioned path. Reverse-engineering Plexamp or the undocumented companion/transcoder internals is contractually prohibited, and Plex holds a broad license over "Interfacing Software" unless you send the written opt-out. Plex gives no API stability guarantee.

### 5.2 Jellyfin licensing
- The server is **GPL-2.0** (https://github.com/jellyfin/jellyfin). Because JellyAmp would talk to it over HTTP (not link/distribute GPL code), **GPL copyleft does not attach to your client**. The official client SDK is **MIT**; official apps (jellyfin-web, jellyfin-desktop) are GPL-2.0 because they reuse GPL components, not because the API imposes it. — https://github.com/jellyfin/jellyfin-desktop , https://www.nuget.org/packages/Jellyfin.Sdk
- Caution: if you copy code from GPL Jellyfin projects (e.g. their player modules) or bundle GPL plugins, those obligations do attach. Also note the Last.fm plugin "cannot be distributed with Jellyfin due to a missing compatible license" — a warning sign for bundling third-party plugins. — https://github.com/jesseward/jellyfin-plugin-lastfm
- Jellyfin API authorization is a documented, stable contract (MediaBrowser scheme), so no license friction there. — https://gist.github.com/nielsvanvelzen/ea047d9028f676185832e51ffaf12a6f

### 5.3 Trademark / naming for "JellyAmp"
- Jellyfin branding rules: "The name 'Jellyfin' is reserved for the Jellyfin project itself and its official clients. 3rd party developers should **not** use the name Jellyfin directly. Additionally, we **discourage** projects from using the combination of 'Jelly[word]' or '[word]fin' to avoid confusion … we strongly request that you develop your own unique name. You **may** reference Jellyfin in a subtitle, description, etc. as an indication of interoperability. For example, 'Floostream, a Jellyfin client' is ideal." Logos are off-limits without permission. — https://jellyfin.org/docs/general/contributing/branding/
- **Conclusion:** the name **"JellyAmp" is exactly the discouraged `Jelly[word]` pattern.** Recommend renaming to a unique brand and using the descriptor "…, a Jellyfin client" (e.g. "Winamp-like player for Jellyfin and Plex"). Avoid "Winamp" in the product name too — it is a trademark of its owner (AOL/Radionomy lineage); "Winamp-compatible skins" is a descriptive/technical use `[UNVERIFIED]` legally, but branding should not imply affiliation.
- Plex marks (PLEX, Plexamp) are proprietary and "any use … without express written permission is strictly prohibited." — https://www.plex.tv/about/privacy-legal/plex-terms-of-service/ (Proprietary Rights section)

---

## 6. Consolidated recommendations for JellyAmp

1. **Jellyfin auth:** implement `Authorization: MediaBrowser` scheme now; do **not** build on `X-Emby-Token` (gone in 12.0). Handle per-user DeviceIds. Target server 10.10+ but expect 10.12/12.0 auth enforcement. — §1.2
2. **Jellyfin playback:** prefer `GET /Audio/{id}/stream?static=true` (full file, best for gapless/ReplayGain); fall back to `/Audio/{id}/master.m3u8` (HLS) or `stream?audioCodec=…&audioBitRate=…` transcode. Build URLs by hand; use `Jellyfin.Sdk` for metadata/session/UserData. — §1.3, §1.8
3. **Jellyfin reporting:** emit `/Sessions/Playing*` every 10 s + on user events, and correct `PlayMethod`; this feeds server-side Last.fm/ListenBrainz plugins. Add a websocket client for `/socket` (remote control, session events). — §1.4, §1.7
4. **Plex auth:** legacy PIN flow for PMS tokens; store per-install `X-Plex-Client-Identifier`; treat JWT as plex.tv-only; use `/api/v2/devices` to obtain the legacy PMS token. — §2.2
5. **Plex playback:** send full client-profile headers incl. `X-Plex-Platform=Generic` (or a real platform) and `X-Plex-Client-Profile-Extra` with music direct-play targets; direct-play `Media[].Part[].key` with `X-Plex-Token`; only transcode when required; version-gate around known transcode regressions. — §2.3, §2.5, §2.10
6. **Plex reporting:** `/:/timeline` for progress, `/:/scrobble` for watched, `/:/rate` for ratings; `X-Plex-Client-Identifier` required. — §2.6
7. **Loudness/gapless are client-side:** read Plex `loudness` metadata and Jellyfin ReplayGain tags; build your own gain/preamp and gapless fader (like Plexamp's Sweet Fades). No server does it. — §3.2, §3.3
8. **Scrobbling:** Last.fm desktop auth (session key) + ListenBrainz token (`Authorization: Token`), with a duplication guard vs server plugins. — §4b
9. **Legal:** rename away from "JellyAmp"; use documented Plex API only (no reverse engineering); keep the client MIT/clean-room to avoid GPL/ToS entanglement; include the privacy/copyright notice Plex requires for "Interfacing Software". — §5

---

*Method note: primary sources only (official docs, GitHub source/PRs, Plex support + forums, NuGet, project branding pages). Items that could not be verified from a primary source are marked `[UNVERIFIED]`.*
