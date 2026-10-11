# KoalaAmp — Technical Specification

**Status:** draft v2 (2026-10-10) — **Qt pivot edition** (v1 was Avalonia/.NET; decision record in `feasibility.md` §1a) · **Preceded by:** `feasibility.md` · **Evidence base:** `docs/research/{skins,visuals,audio,platform,servers,formats,frameworks}.md`

"KoalaAmp" is the working title, pending RL-1 trademark clearance (naming analysis in feasibility §6.3). Target: **v1.0 = "classic Winamp 2.95-era experience, modern guts, three platforms."**

---

## 1. Product definition

### 1.1 Goals
1. Pixel-faithful classic skin (`.wsz`) rendering across main / EQ / playlist windows, incl. windowshade, snapping, `region.txt` shapes.
2. Winamp-semantics audio: 10-band EQ (+preamp, presets, `.eqf`), ReplayGain (album/track), gapless, ~zero-glitch output.
3. MilkDrop visualisations via projectM with the real preset corpus.
4. Local library: folder watch, tag metadata, fast search, smart views, ratings, play counts, Jump-to-File with queue.
5. Server music: Jellyfin (full) + Plex (best-effort) browsing, streaming, scrobble-sync, remote control.
6. Migration path from a real Winamp install: playlists, ML database, EQ presets, bookmarks, config.
7. Platforms: Windows 10+ (x64/ARM64), macOS 14+ (x64/ARM64), Linux X11 (Tier-1; XWayland for Wayland sessions). **Qt 6.12 LTS, C++20.**

### 1.2 Non-goals (v1)
- Winamp 5 **modern (WAL/Maki)** skins — Phase 3 gamble (§12).
- AVS preset compatibility — not portable (feasibility §3.6).
- CD ripping/burning, device sync (MTP), podcast directory.
- Win32 plugin (`in_/out_/dsp_/vis_`) binary loading; we define a C++ plugin API instead (§10).
- Music library *management* (mass retagging) — read metadata, write ratings/playcounts only.

### 1.3 Product principles
- Skin first: default experience is a skinned window, not a "modern app that can load skins".
- Deterministic classic behavior over modern shortcuts: enqueue-vs-play, queue, shuffle/repeat semantics follow Winamp.
- Boring, swappable internals: every external library behind a thin interface with a stub for tests.

---

## 2. System architecture

### 2.1 Process model

```
llama5 (Qt Widgets app)
 ├─ ui/         (WinampWindow set, SkinCanvas, playlist/library views, dialogs)
 ├─ playback/   (TrackScheduler, VlcEngine, DspPipeline, PcmTap, MiniAudioSink)
 ├─ library/    (Scanner, TagReader→TagLib, SQLite/FTS5, smart views)
 ├─ servers/    (jellyfin/, plex/ — ports of koalapaw gateways; Scrobbler)
 ├─ skins/      (WszLoader, SkinSprites, region/viscolor/pledit parsers)
 └─ interop/    (libvlcpp wrapper, projectM-4 C API, TagLib façade)
```

Single process. Qt event loop on the main thread; audio-callback threads (libvlc calls us) touch lock-free ring buffers only — **no GUI calls from audio threads**.

### 2.2 Layering & dependency rules
- `playback`, `library`, `servers`, `skins` reference each other only through abstract interfaces (`ITrackSource`, `ITrack`, engine interfaces).
- `ui` is the only Qt-Widgets-dependent layer (plus `interop/projectM` needing `QOpenGLWidget`).
- Every third-party dep sits in `interop/` with a stub implementation for unit tests.

### 2.3 Key interfaces (contract first)

```cpp
struct Track { SourceId src; std::string id, path, title, artist, albumArtist,
               album, genre; int year, trackNo, discNo; std::chrono::ms duration;
               std::optional<Rg> rg; int rating; /* ... */ };

class ITrackSource {                       // local, jellyfin, plex, playlist
public:
    virtual ~ITrackSource() = default;
    virtual std::vector<Track> search(const Query&, Cancellation&) = 0;
    virtual std::unique_ptr<std::istream> openStream(const Track&, StreamIntent) = 0;
    virtual void setRating(const Track&, int stars) = 0;
};

class IAudioEngine {
public:
    using PcmCb = std::function<void(const float* interleaved, size_t frames,
                                     const PcmFormat&)>;
    virtual void setPcmCallback(PcmCb) = 0;   // decoded, pre-our-DSP
    virtual void play(const MediaRef&) = 0;   // path, URL, or istream
    // pause/seek/volume/...
};
```

---

## 3. Audio engine

### 3.1 Chosen engine: **libvlc 3.0.x (libvlcpp) + miniaudio sink**

Rationale: only mainstream engine with a decoded-PCM callback path *and* the Winamp band table as a native reference; LGPL; in-process C++ (no IPC); audio.md §2.2.

**Configuration profile (per session):**
- Default `aout` untouched (mmdevice/coreaudio/pulse) — used only in bit-perfect mode.
- `--audio-time-stretch` off; `avcodec-hw` off (audio anyway).
- Network sources: `libvlc_media_new_callbacks` with our `ITrackSource::openStream` (Range-capable HTTP via libcurl) so server streams never hit the disk.

**PCM modes — DSP is the default; the EQ interaction rule is load-bearing:**

1. **DSP mode (default):** `libvlc_audio_set_callbacks` (vmem/amem sink) → our float pipeline (§4) → miniaudio device (WASAPI shared / CoreAudio/ALSA/Pulse). The main-window spectrum, visualiser tap, EQ, and ReplayGain all see the same post-DSP samples. **Critical rule (verified in VLC source, 2026-10-10): the "user filters" chain — including the `equalizer` module that `libvlc_media_player_set_equalizer` installs via the `audio-filter` variable — runs *upstream* of the audio output, so the PCM arriving in our callback is post-native-EQ. Therefore in DSP mode we never call `libvlc_media_player_set_equalizer` and we clear `audio-filter`; our biquads (§4.2) are the only EQ.** Enabling native EQ here would double-EQ.
2. **Bit-perfect mode (opt-in):** libvlc owns output end-to-end — its own aout (mmdevice WASAPI shared / CoreAudio / Pulse), native EQ allowed, PCM tap off, our DSP bypassed. We do **not** offer WASAPI-exclusive in this mode: libvlc's mmdevice aout is shared-mode, and exclusive mode requires our pipeline to own the samples (miniaudio `ma_share_mode_exclusive`), which is by definition not bit-perfect-through-libvlc. The skin visibly degrades (Winamp-style flat/dimmed vis area) rather than silently showing nothing. Mode switch restarts the track.
3. **Exclusive-output mode (stretch, Phase 2):** DSP pipeline retained (RG only, EQ off) but routed to miniaudio in exclusive WASAPI at the file's native rate — sample-faithful, not strictly bit-perfect (float math in the RG stage). Only if users ask.

**Output abstraction (DSP mode):** miniaudio 0.12 (public-domain/MIT-0, single .c file) — backends WASAPI (shared), CoreAudio (mac), ALSA + PulseAudio (Linux; Pulse covers PipeWire sessions via the compat socket).

**Measured constraints (FE-2 spike, `docs/spikes/libvlc-callbacks.md` — amend any design that assumes otherwise):**
- **Latency floor is libvlc's decoded block, not the ring:** callback→render p95 measured 50ms (WAV) / 52ms (MP3) / 96ms (FLAC); best xrun-free ALSA p95 91ms. The ≤50ms latency target is **not met on Linux** — ring capacity alone sets latency because VLC fills whatever ring it's given. Back-pressure is not viable (blocking the play callback throttles VLC's clock to 62%). Practical regimes: 200ms ring → 0 underruns, 0.004% overrun (one block per ~8 min); 700ms ring → 0/0 at 1.27s latency. **Implication:** v1 ships a latency selector (compact ~200ms vs relaxed ~700ms) rather than a ≤50ms claim; WASAPI/CoreAudio numbers still unmeasured (owner item). The 30-min soaks prove stability in both regimes.
- **amem (VLC 3.0.x) accepts S16N only** — FL32 requests are rejected. Our tap does one s16→f32 conversion, so >16-bit sources are quantized upstream of us. Accepted for v1 (16-bit tap fidelity); revisit only if VLC 4's amem lands FL32.
- **libvlc native EQ quirks (SE-5/AU-1 traps):** its filter chain applies a −12 dB internal base gain (EQZ_IN_FACTOR = 0.25), and `get_band_frequency()` reports the ISO table while the filter defaults to the Winamp table. Anywhere native EQ is used (bit-perfect mode only, per the rule above), apply a compensating factor; `AU-1` owns the numbers.
- **Bit-perfect seam measured 6.7–18.6ms** on the callback and native-aout paths (both < 50ms), so gapless does **not** force DSP mode on this platform.

**Trackers (MOD/XM/IT/S3M)** — Phase 2, libopenmpt (BSD-3) rendered as a decode source into the same pipeline.

### 3.2 Gapless & crossfade
- Scheduler pre-buffers the next track 10s before end (second libvlc media or pipeline lookahead).
- DSP mode: sample-exact concat at the boundary in our sink. Bit-perfect: measured `stop`/new-media seam in spike FE-2; if > 50ms, gapless forces DSP mode.
- Optional crossfade 0-12s incl. Winamp fade-on-stop/seek semantics (`config_fade`, formats.md §3).

### 3.3 Volume/pan
- DSP mode: master volume + pan are gains in our pipeline (perceptual curve, 0-100 UI).
- Bit-perfect: libvlc volume/stereo-mode.

---

## 4. DSP pipeline (C++)

### 4.1 Signal chain (DSP mode)
```
PCM float32 deinterleaved
 → ReplayGain gain (album|track, preamp, clip prevention)
 → 10× peaking biquads (Winamp table) or Constant-Q variant
 → master preamp gain
 → soft limiter (Winamp "limiter" flag; 1.5:1 knee at −0.1 dBFS)
 → [optional] tempo (SoundTouch, LGPL) — Phase 2
 → interleave → miniaudio sink;  copy → PcmTap ring buffer
```
- Fixed 1024-frame blocks; allocation-free steady state; coefficients recomputed on change only; double-precision internals, float storage. All SIMD-portable scalar code first (`std::simd` if it earns its keep).

### 4.2 EQ semantics
- Bands: 60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000 Hz (Winamp table; verified against VLC's `equalizer_presets.h`).
- Winamp curve mode (default) vs Constant-Q mode (5.51+), selected like `eq_type`.
- Range −12..+12 dB; UI slider value 0-64 (31 = 0 dB); preamp −12..+12 dB; persisted in `.eqf`/settings exactly as Winamp stores them.
- **Native libvlc EQ is used only in bit-perfect mode** (see §3.1 rule).

### 4.3 ReplayGain
- TagLib read: `REPLAYGAIN_TRACK_GAIN/PEAK`, `REPLAYGAIN_ALBUM_GAIN/PEAK`; R128/LUFS tags normalized (`gain = −18 − LUFS`).
- Jellyfin `NormalizationGain` fields; Plex `loudness` metadata → approximate gain (servers.md §3.3).
- Modes off/album/track; preamp; peak-aware clip prevention; −6 dB default fallback for untagged files.

---

## 5. Skins

### 5.1 Classic skin (WSZ) — v1 surface
- Container: ZIP, extension-agnostic (.wsz/.zip/folders). Case-insensitive member names; lenient fallbacks for missing optional files (Webamp behavior).
- Canonical bitmaps (skinspecs + Webamp tables): `main.bmp` (275×116), `titlebar.bmp` (344×87) incl. shade bars + clutterbar, `cbuttons.bmp`, `posbar.bmp`, `volume.bmp` (68×433; 28 frames at `y = n*15`, knob at y=422), `balance.bmp` (same layout; groove starts at x=9 — classic off-by-9 trap), `shufrep.bmp`, `monoster.bmp`, `playpaus.bmp`, `numbers.bmp` (99×13), `nums_ex.bmp`, `text.bmp` (155×18 5×6 font), `pledit.bmp` (280×186), `eqmain.bmp` (275×315: 11 slider grooves, 61-frame thumbs, ON/AUTO/preset panel), `eq_ex.bmp` (275×54 shade), `viscolor.txt` (24 colors), `pledit.txt`, `region.txt`.
- BMP decode must handle 1/4/8/24-bit, **RLE8** (common in real skins), palettes (skins.md §1.2).
- Text files parsed leniently (comments, whitespace, dec/hex RGB).
- **Coordinate table ownership:** one `skinSprites` table ported from Webamp's MIT `skinSprites.ts`, cross-checked against Strider's skinspecs.pdf — *the* compat artifact; changes need screenshot-regression proof.

### 5.2 Rendering (Qt)
- One `SkinnedWindow` (QWidget subclass, `Qt::FramelessWindowHint`) per Winamp window; content = `SkinCanvas`:
  - `paintEvent` composes from pre-decoded `QPixmap` sprite slices — exact rect blits, `Qt::FastTransformation` (nearest-neighbour), no antialiasing on sprite edges.
- Window content sizes at **integer scale** (100%, 200%…). Fractional DPI renders the skin at 1× offscreen and integer-scales, or falls back to uniform scaling via the "crisp" toggle; sprite blits are never fractional-scaled.
- **`region.txt` shapes:** polygons → `QWidget::setMask(QRegion)` — real `SetWindowRgn` on Windows, XShape on X11 (both clip input and output, no compositor needed); macOS uses the CALayer mask (visual clipping) + our app-level point-in-polygon hit-test as the input mask. Compositor alpha is never required for shapes on Win/X11.
- Hot areas: `SkinHitRegion` rects per window state (normal/shade) with pointer handlers; the polygon mask additionally rejects clicks outside the skin everywhere.
- Animation: state-driven `update()` on model change; time loops (spectrum/oscilloscope, title marquee) driven by a `QTimer`-equivalent per-frame callback (16ms) or `QMetaObject` rAF-style ticker on the paint thread's cadence.
- Spectrum/osc: 75 bars/points (real Winamp behavior — `classic_vis.cpp`; 576 is capacity), colors from `viscolor.txt`, Winamp decay curve.

### 5.3 Window management
- `WinampWindowSet`: Player + EQ + Playlist (+ Vis Phase 1.5). Frameless, `setMask` from `region.txt`; `ShowInTaskbar` only Player; tray icon always.
- Snapping: on drag end, snap within 10px (Winamp tolerance) to sibling edges/corners, remembering pair offsets; dock groups move together. **Native Wayland cannot self-position windows → on Wayland sessions the app requests XWayland** (Audacious/Qmmp precedent), or degrades to alpha-masked windows the compositor places.
- Windowshade: player/EQ collapsed height states, alt-titlebar sprites; playlist shade = 1-line bar.
- Z-order: keep-together option (`config_aot`).

### 5.4 Playlist window
- Skinned playlist editor (classic layout, `pledit.txt` theming), resizable by segment tiling (25px tiles per Webamp constants); drag-reorder; enqueue highlight; J jump dialog + queue management; right-click Winamp verbs; ATF title formatting.

### 5.5 Modern skins — Phase 3 research gate only (§12)

---

## 6. Visualisations

### 6.1 Host
- `VisualizerWindow` = `QOpenGLWidget` subclass; projectM instance created in `initializeGL`, rendered in `paintGL` with the context current (the exact pattern projectM's own Qt frontend uses). PCM pushed from `PcmTap` (≤ 33ms or per-frame).
- Windows/X11: context sharing with the app's raster windows is standard Qt; masked-window + GL coexistence is spike FE-1 (risk: setMask vs swapchain — mitigated by hosting viz in its own unmasked toplevel if needed).
- macOS: QOpenGLWidget on the Cocoa platform (layer-masked toplevel unaffected).
- Preset browsing: sidecar skinned list; random/ordered cycling; hot-swap without restart; user preset folders watched.
- Textures: ship projectM base texture pack; per-preset sprites via 4.2 API.

### 6.2 Classic in-main-window vis
- Spectrum + oscilloscope from the same `PcmTap` FFT (576-bin; 75-band projection + raw).

### 6.3 Not in v1
- AVS; MilkDrop3 `.milk2` double-blend; Sonique SVP.

---

## 7. Local library

### 7.1 Scanner
- Roots = user folders; whitelist ext `mp3 flac ogg oga opus m4a aac alac wav wv ape mpc it mod s3m xm` (+cue, Phase 2).
- Mtime/size change detection; incremental rescan; FS watcher + debounce; `.ignore` files; hidden/system skipped.
- Tag read (TagLib 2.3.2): the full §7.1 field set + covers (embedded + `folder.jpg|cover.*|front.*`); duration/bitrate via libvlc pre-scan.
- Ratings/playcount in DB; optional tag write-back — Phase 2, off by default.

### 7.2 Database schema (SQLite, FTS5) — unchanged from v1 draft
```sql
tracks(id PK, path UNIQUE, mtime, size, format, samplerate, channels,
       bitrate, duration_ms, title, artist, album_artist, album, year,
       genre, trackno, discno, composer, publisher, comment, bpm,
       rating DEFAULT 0, playcount DEFAULT 0, lastplayed, dateadded,
       rg_track_gain, rg_track_peak, rg_album_gain, rg_album_peak,
       source TEXT DEFAULT 'local', remote_id TEXT, art_cache_path)
CREATE VIRTUAL TABLE tracks_fts USING fts5(title, artist, album, album_artist,
       genre, composer, content='tracks', content_rowid='id');
```
Smart views: Winamp grammar (`artist has "x" && rating > 3`, `[1 week ago]`) → AST → SQL builder (formats.md §5).

### 7.3 Search & browsing
- FTS5 prefix + ranking; artist/album/genre trees via GROUP BY; default smart views (recently added/played/modified, most played, never played, top rated).
- 100k tracks: search < 100ms p95 on commodity hardware (acceptance).

---

## 8. Server integration — **port of the user's koalapaw code**

**Source of truth:** `~/Code/koalapaw/koalapaw-androidtv/{jellyfin-api,plex-api}` — the maintainer's own clean-room, production Android clients. The C++ port reuses the *wire contracts and flow design*; the Kotlin is a specification we transliterate, not a dependency.

### 8.1 Common abstractions
- `ITrackSource` per server with: auth state, browse trees, search, stream open (Range HTTP), rating ops, playlist sync, remote-control subscription.
- Streams: `openStream` returns a `std::istream` (libcurl, Range, auth headers) → `libvlc_media_new_callbacks` — never temp files.
- Progress/scrobble events flow to `ScrobbleCoordinator` (client Last.fm/ListenBrainz; server reporting separate; anti-double-scrobble setting per server).

### 8.2 Jellyfin (from koalapaw `jellyfin-api` + servers.md §1)
- Auth: `POST /Users/AuthenticateByName` with `Authorization: MediaBrowser Client="KoalaAmp" Device=... DeviceId=<per-user-derived> Version=...`; **no** `X-Emby-Token`, **no `api_key` query param** (both removed in server 12.0). Per-user DeviceId = hash(machineId+userId). Refresh on 401.
- Discovery: UDP broadcast (koalapaw `UdpServerDiscovery`) + manual URL; QuickConnect as secondary flow (koalapaw implements it).
- Browsing: `/Items` by `IncludeItemTypes=MusicArtist|MusicAlbum|Audio`, `ParentId`, `SortBy`, `Recursive`; music windows (koalapaw `musicAlbumsWindow`/`musicArtistsWindow`/A-to-Z offsets) port directly; artwork via `/Items/{id}/Images/Primary`.
- Playback: **direct play** `/Audio/{id}/stream?static=true` through our libcurl layer with the MediaBrowser header (not a tokened URL); HLS fallback `/Audio/{id}/master.m3u8` when transcode needed. `PlayMethod` reported correctly.
- Progress: `/Sessions/Playing` on start, `/Sessions/Playing/Progress` every 10s + on events, `/Sessions/Playing/Stopped` at end; `PlaySessionId` per queue.
- Remote control: WS `/socket` — **auth via header/first-frame handshake, not a query key** (koalapaw's `AuthInterceptor` pattern carries the token in headers; the C++ WS client does the same). Handle `Play`/`Playstate`/`GeneralCommand` + KeepAlive.

### 8.3 Plex (from koalapaw `plex-api` + servers.md §2)
- Auth: plex.tv PIN/link flow (koalapaw `PlexAuth`: `POST /api/v2/pins` → user confirms at plex.tv/link → poll `authToken`); **legacy token for PMS**; resources + connections; headers `X-Plex-Client-Identifier` (stable GUID), `X-Plex-Product/Version/Device-Name/Platform=Generic`, `Accept: application/json`.
- Discovery: GDM (koalapaw `GdmServerDiscovery`).
- Browsing: `/library/sections` → music; `/library/sections/{id}/all` with `X-Plex-Container-Start/Size` paging; children via `/library/metadata/{rk}/children`.
- Playback: direct play `Media[0].Part[0].key` + token header; client-profile headers advertise our codec set; transcode path gated.
- Progress: `POST /:/timeline` (koalapaw `PlexPlayback` — verified shape, signed-in-profile token); ratings `PUT /:/rate` (koalapaw, "verified live"; rating=10 is favorite, −1 unsets); watched `/:/scrobble` + `/:/unscrobble` — **koalapaw deliberately issues these as GET on the ambient-token path, with a P3 note to verify the verb live** (`PlexDetail.kt:402-403`); port that behavior, pin the verb in SV-4's live smoke, record the outcome.
- Posture: version-gate, friendly errors; no sonic-analysis features.

### 8.4 Scrobbling
- Last.fm desktop auth (token→browser→session; MD5 api_sig); now-playing + batched scrobbles.
- ListenBrainz `POST /1/submit-listens` with `Authorization: Token`; MBIDs from Jellyfin ProviderIds.
- Mode: Client / Server(plugin) / Off, default Off; warning when client + server plugin both active.

---

## 9. Migration (from Winamp)

- **Playlists:** M3U/M3U8 (BOM-aware, exact `#EXTINF`), PLS, B4S, XSPF read; write M3U8; ML `playlists.xml` index import.
- **NDE library import:** read-only `main.dat`/`main.idx` (field IDs 0-41; NDETABLE/NDEINDEX) — ratings, playcount, lastplay, dateadded, RG, bpm joined by normalized path.
- **Config:** `paths.ini`, `winamp.ini` (volume/EQ/skin/titlefmt/shuffle/repeat/windowshade), `winamp.q1` presets, `winamp.bm` bookmarks.
- **Skins:** `%APPDATA%/Winamp/Skins` detected + offered for import.
- Idempotent wizard, upsert by path.

---

## 10. Managed plugin API (v2 surface, designed in v1)
C++ ABI slots: `IInputPlugin` (decode→PCM), `IDspPlugin` (float blocks), `IGeneralPlugin`, `IVisualizerPlugin`. Manifest + capability flags. **No Win32 binary plugin loading.**

---

## 11. Platform integration
- Tray icon (QSystemTrayIcon, menu: transport + exit).
- Global hotkeys: `QHotkey`/platform-specific registration; default set mirrors `gen_hotkeys`.
- File assoc + "Enqueue/Play in KoalaAmp" verbs (registry / Info.plist / .desktop `MimeType`).
- Single-instance + local-socket IPC forwarding.
- ATF title formatting subset; default `[%artist% - ]$if2(%title%,$filepart(%filename%))`.
- Updates: GitHub releases check per-OS.

---

## 12. Phase 3 (post-v1) candidates — out of v1
1. Modern skins (WAL + Maki VM) — gated on classic completion; bytecode spec exists (Webamp), class-binding is the cost.
2. AVS via libavs (BSD) — Linux first, 64-bit port.
3. Device sync (MTP). 4. Podcasts. 5. Transcoding.

---

## 13. Testing strategy
- **Golden corpus:** 30 diverse WSZ skins + `base-2.91.wsz`; per-window per-state screenshot diffs at 1×/2× (offscreen `QPixmap` rendering — widget tests, no display server needed on CI).
- **Playback:** test tones (sweeps, gapless pairs, RG-tagged) → tap PCM → assert RMS/spectrum/latency.
- **EQ:** transfer-function sweeps vs reference curves (golden biquad tests); `.eqf` round-trip; **the no-native-EQ-on-callbacks rule is a runtime assertion in FE-2 and a unit test**.
- **Library:** 100k synthetic tracks — scan, FTS p95, smart-view correctness.
- **Servers:** koalapaw-style gateway unit tests against recorded fixtures (Jellyfin 10.10/10.12; PMS 1.4x) + optional live smoke.
- Skin/vis pixel tests run headless where possible; projectM preset parser gets exhaustive unit tests (GL rendering stays on nightly GPU runners).

---

## 14. Observability & settings
- Settings: JSON at platform config path (portable override); schema-versioned, migrations.
- Logging: rolling file + libvlc verbosity bridge; crash reporter opt-in, chosen at PC-1.
- Telemetry: none by default.
