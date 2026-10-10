# FE-5 spike — koalapaw Jellyfin gateway → C++ port skeleton

- **Status:** done (2026-10-10) · **Owner:** FE-5 · **Depends:** FE-0
- **Evidence:** live containers (10.10.7 / 10.11.11 / 12.0) + offline fixture replay
- **Code:** `src/servers/jellyfin/` (port), `tests/jellyfin_port_test.cpp` (tests),
  `src/servers/jellyfin/smoke_jellyfin.cpp` (live smoke), `tests/fixtures/jellyfin/`

## What this proves

One auth flow, one discovery, one browse call, transliterated from koalapaw's
Kotlin onto libcurl + a JSON DOM, end-to-end against a real server and offline
from recorded fixtures. The port covers the *pattern* (result-type seams,
header discipline, coroutine-cancellation → tokens); the full surface is SV-1.

## Pins (decided here, for the whole codebase)

| Dependency | Pin | Why this one | Source |
|---|---|---|---|
| **libcurl** | **8.18.0** (`libcurl4-openssl-dev`, `CURL::libcurl`) | already in `deps/dependencies.json`; Range-capable HTTP for streams later; per-platform acquisition already recorded in `spikes/fe0-deps.md` (distro / vcpkg / Homebrew) | distro package |
| **nlohmann/json** | **3.11.3** | **chosen over simdjson**: the gateway parses small, schema-known objects on a worker thread, not GB/s NDJSON — DOM ergonomics and the FE-0 pin (`scripts/vendor-deps.sh`) win. simdjson's on-demand API would slow this code without measurable gain. | `third_party/include/nlohmann/json.hpp` |
| **SHA-256** | in-tree (`src/servers/jellyfin/Sha256.{hpp,cpp}`) | one hash for the DeviceId; adding OpenSSL as an app dependency for it is heavier than ~120 lines of FIPS 180-4, verified against the standard vectors | this repo |

`scripts/vendor-deps.sh` needed **no change**: libcurl is a native library
(distro/vcpkg/brew), which that script deliberately does not vendor.

## What transliterated 1:1

- **Auth header** (`AuthInterceptor.kt` → `AuthHeaders.cpp`): byte-exact
  `MediaBrowser Client="…", Device="…", DeviceId="…", Version="…"[, Token="…"]`,
  same field order, `Token` present only once a token exists. Asserted by unit
  test against the literal koalapaw output string.
- **UDP discovery** (`UdpServerDiscovery.kt` → `Discovery.cpp`): probe
  `who is JellyfinServer?` → `255.255.255.255:7359`; 4096-byte replies; 300 ms
  receive slices over a 2 s window; `Address` trimmed + trailing `/` stripped;
  blank `Id`/`Name` fall back to `Address`; unusable datagrams dropped, not
  thrown. (One deliberate addition: replies are de-duplicated by `(id,address)`.)
- **Browse window** (`musicAlbumsWindow` → `atoZWindow`): one `GET /Items` with
  `IncludeItemTypes=MusicAlbum`, `Recursive=true`, `SortBy=SortName`,
  `SortOrder=Ascending`, `StartIndex`, `Limit`, `Fields=SortName`,
  `EnableUserData=true`; response `Items`/`TotalRecordCount` → `LibraryWindow`.
- **Auth mapping** (`mapAuthResponse`): `AccessToken` + `User` present → success;
  **401 → InvalidCredentials**; `IOException` → Unreachable; else Failed.
  Album browse: 401 → Unauthorized.

## What needed re-shaping

1. **`sealed`/exceptions → `std::variant` result types.** koalapaw's
   `AuthOutcome`/`ItemsOutcome` become `std::variant<Success, Unreachable, …>`
   (`JellyfinClient.hpp`). Nothing throws across the boundary; `json::parse`
   and transport errors are caught and mapped. The gateway also no longer
   *catches to ignore*: every failure has a named alternative, where koalapaw
   sometimes collapses to `null`.
2. **Coroutine cancellation → a `Cancellation` token.** koalapaw's `Flow`
   checks `currentCoroutineContext().isActive` per receive slice. The C++ port
   threads the existing `library::Cancellation` (spec §2.3) into both HTTP
   (`CURLOPT_XFERINFOFUNCTION` aborts a transfer, surfacing as `"canceled"`)
   and the discovery loop (checked each slice). There is no structured
   concurrency to cancel, so supersession is explicit — the seam `SV-*` needs.
3. **OkHttp interceptor → an `IHttpTransport` seam.** The interceptor becomes a
   compile-time header build plus a transport interface, which is what makes
   fixture replay possible: `FixtureTransport` and `RecordingTransport`
   (`FixtureTransport.cpp`) replay/record exchanges through the *same*
   `JellyfinClient` code path (koalapaw's MockWebServer analogue).
4. **One `JellyfinApi` per (baseUrl, token, deviceId)** (koalapaw's bounded LRU
   of OkHttp clients) is dropped: a spike calls are stateless — the token and
   device are passed in per call, exactly as koalapaw's *pinned* client methods
   do. SV-1 re-introduces a session object if connection reuse earns it.
5. **Per-user DeviceId derivation** — see below; the ticket's formula exists in
   the spec/research, not in koalapaw's source. Most significant finding.

## Finding: the DeviceId derivation

The ticket (and `servers.md` §1.2, and Jellyfin's own SDK author) say
*per-user DeviceId = hash(machineId, userId)*. **koalapaw does not do that.**
Its actual derivation is:

- `EncryptedServerStore.kt` `init`: one **install id**, `UUID.randomUUID()`,
  minted once and persisted (`KEY_DEVICE_ID`);
- `ManageUsersViewModel.kt:202` / `FamilySetupViewModel.kt:239`
  `deviceIdForUser()`: **reuse the profile's stored `deviceId`, else mint a
  fresh `UUID.randomUUID()`**, stored *per profile*;
- the regular password sign-in path (`PlexLinkSignInViewModel.kt:209`) even
  reuses the *install* id.

So koalapaw's invariant is not "a hash" — it is **stable-per-user and
never-shared**, achieved by persisting a random UUID per profile. The trap it
guards is documented at `AuthInterceptor.kt:17` and pinned by
`AuthInterceptorTest.kt:109-112` ("the install-time device id must never leak
into the header, or Jellyfin re-binds the device and evicts other profiles'
tokens"); the ticket's `JellyfinGateway.kt:98` pointer is where that comment
used to sit between revisions — the live text is the ones above.

**Port decision.** JellyAmp keeps the invariant but makes it *deterministic*
and stateless, per the ticket: `deriveDeviceId(machineId, userKey) =
hex(sha256(machineId + ":" + userKey))` (`DeviceIdentity.cpp`). `userKey` is the
username at sign-in (the Jellyfin user id is only known *after* the first
authenticate, so it cannot seed that call; the username is unique per server
and stable). `MachineIdStore` ports koalapaw's install id (random UUID,
persisted once; XDG state path). This is strictly stronger than koalapaw's
persisted-UUID approach: stable across a lost profile store, no second
persisted secret, and no chance of two profiles colliding on the install id.

## DeviceId re-login loop (the silent breaker)

Verified live on all three servers with `--auth-twice`:

- auth #1 and auth #2 with the **same derived DeviceId both succeed** — no
  `400`/`401`, no re-bind rejection;
- **but** Jellyfin keeps *one* token per DeviceId, so auth #2 **evicts**
  token #1 (`changed=yes` in the smoke output). A client that re-authenticates
  and keeps using the old token gets `401` on the next call. The smoke now
  adopts the refreshed session — this is exactly the failure mode the ticket's
  risk note predicted, and it is *token* invalidation, not DeviceId rejection.

## Live vs fixture status

Live (Docker, host port → `8096`): servers stood up headlessly (startup wizard
+ admin + `POST /Library/VirtualFolders` music library + scan over a generated
3-track/2-album mp3 tree), then the smoke ran auth → discovery → album browse.

| Server | Digest | Discovery | Auth ×2 | Album browse | Result |
|---|---|---|---|---|---|
| Jellyfin **10.10.7** | `sha256:7ae36aab…648ce2` | ✅ found (2 servers) | ✅ both ok | ✅ `total=2` — Blue Train, Kind of Blue | pass (exit 0) |
| Jellyfin **10.11.11** | `sha256:aefb67e6…b35db` | ✅ | ✅ both ok | ✅ `total=2` | pass (exit 0) |
| Jellyfin **12.0.0** | `sha256:baba6304…d5ef5` | ✅ | ✅ both ok | ✅ `total=2` | pass (exit 0) |

**10.12 was not testable:** Jellyfin never shipped a stable `10.12` image (only
2023 `10.12-unstable` tags); `10.11.11` is the last stable 10.x and `12.0` is
where the deprecated token transports are actually removed — the more valuable
upper bound, so both were used. Digest pins above replace the ticket's
unresolvable `10.10.*`/`10.12.*` name pins.

Discovery note: the Jellyfin containers are on the Docker bridge, so the smoke
was pointed at the bridge broadcast address (`--broadcast 172.17.255.255`);
`255.255.255.255` cannot cross into a bridge netns. All three replied with the
JSON shape koalapaw parses.

Offline: `tests/fixtures/jellyfin/` holds three hand-written **sanitized**
exchanges (auth, re-login auth, album page) in the `RecordingTransport` format;
`tests/fixtures/jellyfin-unauthorized/` holds a 401. They replay through the
same `JellyfinClient` path (`jellyfin_port_tests`, 11 cases / 104 assertions,
all passing). Fixtures are sanitized by design — live recordings contain real
tokens (the smoke's `--record DIR` produces them and they are **not** committed).

## Deviations & follow-ups for SV-1

- Per-user DeviceId is a hash (ticket) rather than a persisted random UUID
  (koalapaw) — invariant preserved, see above.
- Discovery de-duplicates replies; koalapaw emits per datagram.
- `parentId` is omitted from `/Items` when empty (top-level browse); koalapaw
  always passes a library-view id.
- Not ported (out of scope): QuickConnect, user listing, letter-offset rail,
  progress/scrobble, stream open, WS remote control.
- SV-1 must decide the session object (token refresh on 401, one cached
  transport) and whether `userKey` keys on the user id once known.

## How to reproduce

```sh
# build
cmake --preset linux-gcc && cmake --build build/linux-gcc
# offline tests
ctest --test-dir build/linux-gcc --output-on-failure
# live (any Jellyfin with a music library)
build/linux-gcc/src/servers/jellyfin/jellyfin_smoke \
  --url http://127.0.0.1:8096 --user admin --password … --auth-twice \
  --discover --broadcast 172.17.255.255 --parent <music-library-id>
```
