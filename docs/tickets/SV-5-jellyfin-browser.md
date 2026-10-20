# SV-5 · Jellyfin library browser + play

- **Milestone:** M3 · **Est:** 3d · **Depends:** SV-1, SV-2 · **Status:** in-review

## Story
A signed-in server becomes playable: browse its music libraries, albums and (read-only) playlists, then double-click a track to play it through the SV-2 callback stream and see it reported. This is the missing link between SV-1's gateway and SV-2's playback stack — until now the app could sign in but had no way to reach a server track.

## Tasks
- [x] Libraries: `GET /Users/{userId}/Views`; expose `CollectionType` so the browser can filter to `music` (movies/TV views are ignored). — `JellyfinClient::musicLibraries`; fixture `14-views.json` returns music + movies and the test pins the distinction.
- [x] Album tracks: `GET /Items?ParentId={albumId}&IncludeItemTypes=Audio&Recursive=true&SortBy=ParentIndexNumber,IndexNumber`, mapped through the same DTO→`Track` helper `search` uses. — `trackFromDto` now backs search, album contents and playlist items; fixture `15-album-tracks.json` (order, codec, `NormalizationGain`).
- [x] Read-only playlists: list via `/Items?IncludeItemTypes=Playlist&Recursive=true`; items via `GET /Playlists/{playlistId}/Items?userId=…` (playlist order, not sort order). No create/edit/delete. — `JellyfinClient::playlists`/`playlistItems`; fixtures `16`/`17`.
- [x] `JellyfinSource : IServerSource`: `search` (whole library), `openStream` (static direct stream), `setRating` (`POST /Users/{userId}/Items/{itemId}/UserData` with `Rating = stars × 2`), `isAuthenticated`/`signOut`. Closes SV-1's open gateway-interface checkbox. — `JellyfinSource` + fixture `18-rating.json`; the search→stream→rate→sign-out path is one test.
- [x] Persistent `ServerSession` (in memory only): the `ServerAuth` survives the connection dialog closing, so browsing/playback can start after sign-in. Token never persisted. — `activeServer` in `main.cpp` is updated by `ConnectionDialog::sessionChanged`; the reporter holder reuses the same copy, and a session change closes a stale browser window. After an app restart the token is gone by design (`ServerBook` is identity-only), so selecting the active server — or opening the dialog — prefills URL/user and focuses the password: re-login is password + Enter, not remove + re-add. `Browse server…` while signed out opens that dialog. Staying signed in across a restart is SV-6: the access token goes in the OS keychain, and `servers.json` stays identity-only.
- [x] `ServerBrowserWindow` (skinned `gen` chrome like the connection manager): panel 1 libraries, panel 2 albums + playlists, panel 3 tracks. Double-click plays; "Play next" enqueues (reuses `PlaybackCore::enqueueNext`). Recently browsed paths are kept in memory per window. — `src/ui/ServerBrowserWindow.{hpp,cpp}`: libraries + playlists share the left column, albums the middle, tracks the right; music views are filtered from `CollectionType`, playlists are read-only, and a 401 emits `sessionExpired`. A **Browse** button in the connection dialog (enabled on sign-in) emits `browseRequested` and shares the main menu's open action — the menu entry alone was too hidden to find.
- [x] Play path: `JellyfinPlayback::plan(baseUrl, token, deviceId, track, reporter.playSessionId())` → `PlaybackCore::play` (album/playlist queues the rest). Reporter and WS client attach to the same session (SV-2 classes already exist). — Menu → **Browse server…** opens the window; the handler plans each track, plays the selected one and enqueues the rest; "Play next" uses `enqueueNext`. Reporter is attached on the same session, so the dashboard/PlayMethod/`PlaySessionId` path is live.
- [x] Offline fixtures/tests: views, album tracks, playlist list, playlist items, source search/openStream, rating POST. UI smoke for the browser window with a fixture transport. — Gateway/source (`jellyfin_browse_tests`, 51 assertions) and the window (`jellyfin_browser_window_tests`, 28 assertions: music-view filtering, auto-loaded albums/playlists, album→tracks, double-click play callback, playlist mode).
- [ ] Live-container pass: browse → play → dashboard shows position; this is also what unlocks SV-2's two unrun acceptance items. — Implementation reachable end to end; the container run is the remaining step.

## Acceptance
- Sign in, browse into a music library, open an album, double-click a track: it plays through the callback stream, the server dashboard reports DirectPlay at the right position, and the next track is queued gaplessly.
- A playlist opens in playlist order and plays end to end.
- `JellyfinSource` passes the `IServerSource` contract (search/openStream/setRating) with fixture replay; a 401 surfaces as an auth error, not silence.
- Static: no token in any browsed URL or playlist entry (grep over fixtures/handshakes).
- Browser window renders with and without `gen.bmp`/`genex.bmp` (fallback), consistent with the connection manager.

## Out of scope
- Playlist create/reorder/delete (Phase 2.5).
- Artist drill-down UI (the gateway has artist pages; the browser starts at libraries → albums/playlists → tracks).
- Multi-server simultaneous sessions (single active server).
- Plex (SV-3/SV-4).

## References
- `spec.md` §8.2 — browsing, playback and reporting contracts
- koalapaw `musicArtistsWindow`/`musicAlbumsWindow`/`AtoZ` query shapes (already ported in SV-1)
- `docs/research/servers.md` §1.3-1.4
- `docs/tickets/SV-1-jellyfin-port.md`, `SV-2-jellyfin-playback.md`
