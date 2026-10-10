# Winamp Data Formats & Feature Surface for a Compatible Clone

_Research date: 2026-10-10. Classic/5.x focus. All load-bearing claims cite a URL; items that could not be corroborated are marked_ `[UNVERIFIED]`.

## 0. Source-vitality snapshot (needed context)

| Item | Version / state | License | Last release |
|---|---|---|---|
| Official Winamp (Windows) | 5.9.2 | Proprietary | Apr 2023 (5.8 in 2018; 5.9 Sep 2022) — https://en.wikipedia.org/wiki/Winamp |
| Winamp source ''community'' release | Sep 24 2024, repo deleted Oct 16 2024 | Winamp Collaborative License (not OSI) | n/a — https://www.theregister.com/software/2024/10/16/releasing-winamp-source-goes-badly-for-its-owners-anyway/417709 |
| Community source mirror (reference copy) | branch `community` | Non-permissive | mirror lives on — https://github.com/manfromafar/winamp , alternate https://git.axenov.dev/Museum/winamp |
| WACUP (community Winamp continuation, DrO) | preview 1.99.55.25130 | Free, closed source | Sep 30 2026 — https://getwacup.com/ |
| Webamp / Webamp Modern | classic skins JS + modern-skin Maki POC | MIT | active (classic); modern on ice — https://jordaneldredge.com/webamp-modern |
| NDE readers | node-nde (MIT, JS), xnde v0.1.4 (GPLv3, Rust, 2023), ndephp/neuralys (PHP) | see links | — https://github.com/Wiiseguy/node-nde / https://docs.rs/crate/xnde/latest |

**Legal caveat:** the 2024 source release is *not* open source (no-fork/contribution-assignment terms) and was pulled after including third-party code. Use it as a *format reference*, not as a code base; prefer clean-room implementations and independently licensed reimplementations (Webamp MIT, node-nde MIT, xnde GPLv3). Webamp's author explicitly flags the derivative-work risk — https://jordaneldredge.com/webamp-modern.

---

## 1. Playlist formats

Winamp's own playlist readers/writers live in `Src/playlist/` (https://github.com/manfromafar/winamp/tree/community/Src/playlist).

### M3U / M3U8 (`.m3u`, `.m3u8`) — primary format
- **Writer** emits literally: header `#EXTM3U\n`; per-track with metadata `#EXTINF:<length_seconds>,<title>\n<filename>\n`; without metadata just `<filename>\n`. (M3UWriter.cpp: `Open` → `fprintf(fp,"#EXTM3U\n")`, `Write` → `fprintf(fp,"#EXTINF:%d,%s\n%s\n",length,title,filename)`.) https://raw.githubusercontent.com/manfromafar/winamp/community/Src/playlist/M3UWriter.cpp
- **M3U8** = UTF-8 M3U; Winamp's writer emits a UTF-8 BOM `EF BB BF` *then* `#EXTM3U`; titles/filenames are encoded UTF-8. https://raw.githubusercontent.com/manfromafar/winamp/community/Src/playlist/M3U8Writer.cpp
- **Loader** recognises directives: `#EXTM3U`, `#EXTINF:`, `#UTF8`, and (2020 cloud era) `#EXT-X-NS-CLOUD:`; detects UTF-8 from a BOM or from the `.m3u8` extension; strips an `ASF ` m3u prefix; resolves relative paths against the playlist's own folder; ignores unknown `#` lines. https://raw.githubusercontent.com/manfromafar/winamp/community/Src/playlist/M3ULoader.cpp
- **`#EXTAUTOSTART` is NOT a Winamp extension.** There is no occurrence in the source; it is an iTunes/other-player extension. Do not implement it as ''Winamp'' — `[UNVERIFIED as a Winamp format]`.

### PLS (`.pls`) — INI-style
- Structure: `[playlist]` section; `NumberOfEntries=<n>`, `Version=<0|1>`, then `File1=`, `Title1=`, `Length1=` (seconds) … Loader uses `GetPrivateProfileIntA/StringA`; `Version>=1` implies Title/Length present. https://raw.githubusercontent.com/manfromafar/winamp/community/Src/playlist/PLSLoader.cpp

### B4S (`.b4s`) — Winamp 3/5 XML playlist
- XML with root `WasabiXML` **or** `WinampXML`; element `<playlist>` containing `<entry playstring="file://…">` with child `<name>` and `<length>` (milliseconds). Loader strips `file://`/`file:` prefixes. https://raw.githubusercontent.com/manfromafar/winamp/community/Src/playlist/B4SLoader.cpp ; writer exists (B4SWriter.cpp).

### XSPF (`.xspf`)
- Winamp can read/write XSPF (XML Shareable Playlist Format): `Src/xspf/XSPFLoader.cpp`, `XSPFHandler.cpp`. https://github.com/manfromafar/winamp/tree/community/Src/xspf

### Winamp Library playlist **index XML** (the ''playlist XML export'' question)
- Stored at `<user settings>\plugins\ml\playlists\playlists.xml`; root `Winamp:Playlists` (or `playlists`); per entry `<playlist filename=… title=… songs=… seconds=… iTunesID=… cloud=… id="{GUID}">`. Confirmed from the reader: https://raw.githubusercontent.com/manfromafar/winamp/community/Src/playlist/PlaylistsXML.cpp
- Actual playlist contents are ordinary `.m3u` files in that same folder; this XML is only the index/metadata.

### iTunes library XML (import/export)
- `ml_impex` writes/reads an **Apple plist 1.0** XML (`<!… -//Apple Computer//DTD PLIST 1.0//EN …>`), enabling library backup/rotation between Winamp and iTunes. https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Plugins/Library/ml_impex/itunesxmlwrite.cpp

### Also supported
- ASX/ASXv2 and WPL handlers exist in core (`asx.cpp`, `ASXv2.cpp`). https://github.com/manfromafar/winamp/tree/community/Src/Winamp

**Clone takeaway:** read/write M3U + M3U8 (BOM-aware), PLS, B4S, XSPF, ASX/WPL; write ML playlists as M3U + playlists.xml index. Match Winamp's exact `#EXTINF` emission for round-trip fidelity.

---

## 2. Winamp Library DB — the Nullsoft Database Engine (NDE)

**There is no `winamp.mdb`.** The user library is `main.dat` + `main.idx` under `%APPDATA%\Winamp\Plugins\ml\`; history is `recent.dat`/`recent.idx`; view metadata is `met*.vmd`. The names `winamp.mdb` and `nscdb` do not appear in the source or in the community format documentation — treat them as misconceptions. `[UNVERIFIED / likely false]`
- File locations: https://diymediahome.org/backing-copying-winamp-media-library-database ; https://github.com/Wiiseguy/node-nde (''main.dat and main.idx … in Winamp's Plugins\ml folder'').
- Engine: **Nullsoft Database Engine (NDE)**, codenames ''Near Death Experience'' / ''Neutrino''. Wiki stub: https://web.archive.org/web/20240118132257/http://wiki.winamp.com/wiki/Nullsoft_Database_Engine . Source: https://github.com/manfromafar/winamp/tree/community/Src/nde

### On-disk format
Documented by the original SDK spec and independently re-derived by xnde:
- Data file signature = `NDETABLE` (8 bytes). Index file signature = `NDEINDEX` (8 bytes), then u32 record count, then a primary index array of u32 offsets into the data file, then optional secondary indexes. https://web.archive.org/web/2024/http://gutenberg.free.fr/fichiers/SDK%20Winamp/nde_specs_v1.txt ; https://docs.rs/crate/xnde/latest
- Record 0 = column definitions; record 1 = index definitions; record 2+ = data records. Each record is a linked list of fields.
- Field header: `u8 columnID, u8 type, u32 dataSize, u32 nextPos, u32 prevPos, <data>`.
- Field types (source `TranslateObject`, nde_specs): `COLUMN=0, INDEX=1, REDIRECTOR=2, STRING=3, INTEGER=4, BOOLEAN=5, BINARY=6, GUID=7, FLOAT=9, DATETIME=10, LENGTH=11`, with newer source adding `INT64=13, BINARY32=14, INT128=15`. String/Binary payloads are u16-length-prefixed and **not** NUL-terminated. Unknown types are tolerated (error count, no hard fail). https://raw.githubusercontent.com/manfromafar/winamp/community/Src/nde/DBUtils.cpp ; https://raw.githubusercontent.com/manfromafar/winamp/community/Src/nde/nde.h

### Schema — ml_local table columns (authoritative field IDs)
From `ml_local.h` / `db.h`: https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Plugins/Library/ml_local/ml_local.h (and db.h)

`0 filename, 1 title, 2 artist, 3 album, 4 year, 5 genre, 6 comment, 7 trackno, 8 length(sec), 9 type(0=audio,1=video), 10 lastupd, 11 lastplay, 12 rating, 13 (reserved), 14 GracenoteID (legacy), 15 playcount, 16 filetime, 17 filesize (bytes since 5.7; KB before), 18 bitrate(kbps), 19 disc, 20 albumartist, 21 albumgain (ReplayGain), 22 trackgain (ReplayGain), 23 publisher, 24 composer, 25 bpm, 26 discs, 27 tracks, 28 ispodcast, 29 podcastchannel, 30 podcastpubdate, 31 GracenoteFileID, 32 GracenoteExtData, 33 lossless, 34 category, 35 codec, 36 director, 37 producer, 38 width, 39 height, 40 mimetype, 41 dateadded`

- Ratings are **1–5** (0/empty = unrated). ReplayGain is stored in DB columns 21/22 (plus written to tags).
- A separate ''extended fields'' mechanism/table holds optional extras (`podcast*`, `Gracenote*`, `cloud`, `realsize`, …); exact byte layout is `[UNVERIFIED]`, but the logical model is confirmed by node-nde's output field list: `filename title artist album year genre comment trackno length type lastupd lastplay rating tuid2 playcount filetime filesize bitrate disc albumartist replaygain_album_gain replaygain_track_gain publisher composer bpm discs tracks ispodcast podcastchannel podcastpubdate GracenoteFileID GracenoteExtData lossless category codec director producer width height mimetype dateadded`. https://github.com/Wiiseguy/node-nde

### Known readers (use one for the importer MVP)
- **node-nde** (JS, MIT) — `NDE.load('main.dat','main.idx').readAll()`; can attempt to read `.dat` alone. https://github.com/Wiiseguy/node-nde
- **xnde** (Rust, GPLv3, v0.1.4 2023-05-17) — `xnde export -f json main.idx main.dat`; documents the format. https://docs.rs/crate/xnde/latest
- **c-nde** (C) https://github.com/Wiiseguy/c-nde ; **ndephp** (PHP) https://github.com/daniel15/ndephp ; **neuralys/nde** (PHP) https://github.com/neuralys/nde ; Delphi reader http://www.stef.be/dev/delphi/winamp/source.html

**Clone takeaway:** ship a one-way NDE importer to migrate users' ratings/playcounts/lastplayed/dateadded/ReplayGain, but use your own modern DB internally. Writing NDE is unnecessary and risky.

---

## 3. Preferences / config files

Winamp splits config across several files; folder resolution is non-obvious.

### `paths.ini` (in the program folder) — path redirection
`[Winamp]` keys, read by `LoadPathsIni`: `inidir`, `m3udir`, `m3ubase`, `inifile`, `class`, `cwd`. This lets Winamp run portable / with the ini and playlists elsewhere. https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Winamp/PathsINI.cpp

### `winamp.ini` (section `[Winamp]`) — core player state
All keys map to the `config_*` globals; Winamp writes INI values as UTF-8 (`config_utf8` default 1). Representative keys (`Src/Winamp/config.h`):
`config_volume(200)`, `config_pan`, `config_shuffle`, `config_repeat`, `config_aot`, `config_autoload_eq`, `config_use_eq`, `config_preamp(31)`, `config_outname("out_ds.dll")`, `config_dspplugin_name`, `config_visplugin_name`, `config_skin`, `config_titlefmt("[%artist% - ]$if2(%title%,$filepart(%filename%))")`, `config_useexttitles`, `config_pe_fontsize/direction`, `config_windowshade`, `config_artwork_filter("cover")`, and geometry for player/playlist/EQ/video windows. https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Winamp/config.h

EQ-specific keys (documented for 5.51+): `eq_type` (0 = Winamp/4Front, 1 = Constant-Q), `eq_frequencies` (0 = Winamp bands, 1 = ISO bands), `eq_limiter` (clipping prevention). https://meggamusic.co.uk/winamp/docs/winamp_551_eq_changes.htm

### `studio.xnf` — modern (Wasabi) skin settings
Per-user settings for modern/Wasabi skins (window positions, colours, skin-specific values), written by the Wasabi config system (`Src/Agave/Config/api_config.h`). Commonly reported as residing in `%APPDATA%\Winamp`; note the DIY-media page spells it `studio.nxf`. Exact filename/format not re-verified from primary source — `[UNVERIFIED: name and XML structure]`. https://diymediahome.org/backing-copying-winamp-media-library-database ; https://github.com/manfromafar/winamp/tree/community/Src/Agave/Config

### Other notable files (per community file inventory)
- `winamp.q1` — **Equalizer presets** (also `winamp.q2` reported as a default/alternate preset).
- `winamp.bm` / `winamp.bm8` — **bookmarks** (`BOOKMARKFILE` in config.h).
- `gen_ml.ini` — **media library views/playlists config**.
- `recent.dat` / `recent.idx` — **play history** (NDE).
- `met*.vmd` — **library view metadata** (smart views).
- `milk2.ini` — **MilkDrop settings**.
- Language packs: `.wlz` (Winamp Language Zip).
Source: https://diymediahome.org/backing-copying-winamp-media-library-database ; https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Winamp/config.h

**Clone takeaway:** implement a portable settings layer that can *read* `paths.ini` + `winamp.ini` (at least: volume, EQ/preamp/limiter, skin, output/vis selection, title format), and import `winamp.q1` EQ presets and `winamp.bm` bookmarks.

---

## 4. Media Library plugin history — what mattered

`gen_ml.dll` is the Media Library window; all `ml_*.dll` require it and the ML is empty without at least one. Core set and purposes (installer documentation + source tree): https://pawelporwisz.pl/winamp/Help/Windows/help_windows_start.php ; https://github.com/manfromafar/winamp/tree/community/Src/Plugins/Library

| Plugin | Feature | Relevance today |
|---|---|---|
| `ml_local` | Local Media: scan folders, DB, **smart views/query**, mass tag edit, album art | **Essential** |
| `ml_playlists` | Manage/store playlists in ML | **Essential** |
| `ml_bookmarks` | Bookmarks | Wanted (small) |
| `ml_history` | Play history/counts | **Essential** |
| `ml_nowplaying` | Info/web pages for current track | Wanted |
| `ml_wire` (''Shoutcast Wire'') | Podcast directory + RSS/Atom subscription + downloads (Opus/OPML/RSS parsing, background downloader) | **Wanted** (podcasts) — https://github.com/manfromafar/winamp/tree/community/Src/Plugins/Library/ml_wire |
| `ml_disc` | CD rip/burn UI | Legacy (rip maybe; burn dead) |
| `ml_rg` | ReplayGain analysis (writes RG tags/DB) | **Wanted** — https://github.com/manfromafar/winamp/tree/community/Src/Plugins/Library/ml_rg |
| `ml_transcode` | Format converter | Wanted (nice-to-have) |
| `ml_impex` | iTunes XML import/export (library portability) | Useful |
| `ml_autotag` | Gracenote MusicID auto-tagging | Dead (Gracenote service ended) |
| `ml_pmp` | Portable device sync (plugins pmp_ipod/p4s/usb/activesync/njb/android) | Mostly dead-tech; MTP/USB still useful — https://pawelporwisz.pl/winamp/Help/Windows/help_windows_library.php |
| `ml_online` | AOL Radio/Video, Winamp Music, In2TV/OurStage, SHOUTcast listings | **Dead** (AOL/AOD) |
| `ml_downloads`, `ml_fanzone`, `ml_webdev`, `ml_nft` | Store/artists/NFT | Dead

Dead-tech to explicitly drop: AOD/AOL on-demand and AOL online services, ml_fanzone/ml_nft, CD burning, Gracenote auto-tag server, iPod''s iTunesDB sync (unless deliberately kept via libgpod). Still-wanted: local library + smart views, playlists, bookmarks, history, ReplayGain, podcasts, transcoding, MTP/USB device sync.

---

## 5. Feature surface (classic Winamp differentiators)

- **10-band EQ + preamp**: two algorithms — original Winamp/4Front and Constant-Q (5.51+); two band tables — Winamp (70,180,320,600,1000,3000,6000,12000,14000,16000) or ISO (31.5,63,125,250,500,1000,2000,4000,8000,16000). Clipping limiter default on. Band values are 0–64 with 31 = 0 dB (0 = +12 dB, 63 = −12 dB); preamp likewise (`In_Module.EQSet(on,data[10],preamp)`). Presets live in `winamp.q1`. Options include **auto EQ per track** (`config_autoload_eq`). https://meggamusic.co.uk/winamp/docs/winamp_551_eq_changes.htm ; https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Winamp/IN2.H ; https://pawelporwisz.pl/winamp/Help/Windows/help_windows_preferences.php
- **ReplayGain**: modes album/track (''preferred source''), ''Apply gain / prevent clipping'', ''disable if preferred source unavailable'', fallback gain for files lacking RG (**default −6.0 dB**), and a preamp. In-modules may self-handle RG via `IN_MODULE_FLAG_REPLAYGAIN` / `IN_MODULE_FLAG_REPLAYGAIN_PREAMP` (5.54+). Values are both written to file tags and stored in NDE columns 21/22. https://pawelporwisz.pl/winamp/Help/Windows/help_windows_preferences.php ; https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Winamp/IN2.H ; https://replaygain.hydrogenaudio.org/player_preamp.html
- **Jump to File (J)**: built-in basic dialog; **JTFE (gen_jumpex, DrO)** ships with Winamp and adds a persistent **queue** (Q key / Alt-click), enqueue-vs-play semantics, Move-after-current, ''load playlist into queue'', queue highlighting, and shell ''Enqueue & Play'' verbs. https://pawelporwisz.pl/winamp/Help/Windows/help_windows_preferences.php (sections 3.2.3, 3.2.9)
- **Enqueue vs play**: distinct shell verbs (`Winamp.Play`, `Winamp.Enqueue`, `Winamp.Bookmark` in config.h) and a ''double-click enqueues'' option — https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Winamp/config.h
- **Global hotkeys**: `gen_hotkeys` — configurable actions incl. rate track; optional `WM_APPCOMMAND` media-key capture. Source: https://github.com/manfromafar/winamp/tree/community/Src/Plugins/General/gen_hotkeys
- **Now Playing**: `ml_nowplaying` view (info/web pages for current track). https://pawelporwisz.pl/winamp/Help/Windows/help_windows_library.php
- **Ratings 1–5 stars**: NDE `rating`; ability to write ratings into tags added in 5.6 (MP3/WMA/Ogg/FLAC). https://en.wikipedia.org/wiki/Winamp
- **Smart views / query language**: full documented grammar (fields, `=`/`!=`/`<`/`>`/`HAS`/`NOTHAS`/`LIKE`/`BEGINS`/`ENDS`/`ISEMPTY`, `&&`/`||`/`!`, relative datetimes like `[1 week ago]`). Default smart views: Recently Added/Played/Modified, Most Played, Never Played, Top Rated, Podcasts (`TREE_IMAGE_LOCAL_*`). Search box accepts `?`/`query:` prefix. https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Plugins/Library/ml_local/queries.txt ; https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Plugins/Library/ml_local/ml_local.h
- **CD ripping**: `ml_disc` + encoder plugins (`enc_lame.dll` LAME MP3, `enc_fhgaac.dll` Fraunhofer AAC/HE-AAC, `enc_wma`, `enc_wav`, `enc_flac`, `enc_vorbis`); CD lookup via Gracenote (`CDDBControlWinamp.dll`). MP3/AAC encoding was ''Pro''-gated (open from 5.666/5.8). **CD burning** used the Sonic engine (`pxsdkpls.dll`) — dead. https://pawelporwisz.pl/winamp/Help/Windows/help_windows_start.php
- **Transcoder**: `ml_transcode` (Send To → Format Converter). https://pawelporwisz.pl/winamp/Help/Windows/help_windows_start.php
- **Scrobbling**: Last.fm/AudioScrobbler shipped as a plugin; exact plugin id `[UNVERIFIED]`.
- **Portable sync**: `ml_pmp` + `pmp_ipod` (iPod Classic/Nano/Shuffle, no iTouch/iPhone), `pmp_p4s` (PlaysForSure/MTP), `pmp_usb`, `pmp_activesync` (WinMobile), `pmp_njb` (Creative), `pmp_android`; album-art support varies by plugin. Dead-tech: njb/activesync/wifi-Android; still useful: USB/MTP. https://pawelporwisz.pl/winamp/Help/Windows/help_windows_library.php
- **Misc still expected**: advanced title formatting (ATF) with `%field%` tokens; bookmarks; classic spectrum/oscilloscope vis; language packs (.wlz); Winamp Agent for file associations.

---

## 6. Plugin / API surface (gen_*, in_*, out_*, dsp_*, vis_*, enc_*)

All are **Win32 C DLLs** with `__cdecl`/`__stdcall` entry points; headers in the source:
- **General** `gen_*.dll` — export `winampGetGeneralPurposePlugin()` returning `winampGeneralPurposePlugin` (`GPPHDR_VER 0x10`, unicode `0x11` in 5.64+). https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Winamp/GEN.H
- **Input/decoder** `in_*.dll` — export `winampGetInModule2()` returning `In_Module` (`IN_VER 0x101`; `IN_UNICODE`; flags `IN_MODULE_FLAG_USES_OUTPUT_PLUGIN`, `IN_MODULE_FLAG_EQ`, `IN_MODULE_FLAG_REPLAYGAIN`, `IN_MODULE_FLAG_REPLAYGAIN_PREAMP`). Supplies metadata, playback, and vis/PCM feed (`SAAddPCMData`, `VSAAdd`). https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Winamp/IN2.H
- **Output** `out_*.dll` — export `winampGetOutModule()` returning `Out_Module` (`OUT_VER 0x10/0x11`; known ids: waveout 32, gapless 64, xfade 63, disk 33, dsound 38, NULL 65, mm2 69, wasapi 70). https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Winamp/OUT.H
- **DSP** `dsp_*.dll` — export `winampDSPGetHeader2()` returning `winampDSPHeader` (`DSP_HDRVER 0x20`, 0x22 with HWND); `ModifySamples(...)` in-place processing. https://raw.githubusercontent.com/manfromafar/winamp/community/Src/Winamp/DSP.H
- **Visualization** `vis_*.dll` — `vis.h` (window + PCM/spectrum callbacks). https://github.com/manfromafar/winamp/tree/community/Src/Plugins/Visualization
- **Encoder** `enc_*.dll` — used by ripper/transcoder (see §5).

**IPC**: Winamp exposes `WM_WA_IPC` to its main window; `wParam` = command id, `lParam` = data. Examples: `IPC_GETVERSION=0`, `IPC_GETPLAYLISTFILE`, `IPC_GETLISTPOS`, `IPC_SETPLAYLISTPOS`. Header: `wa_ipc.h`; mirrors: https://searchcode.com/total-file/4056117/sdk/winamp/wa_ipc.h ; https://github.com/hugovk/winamp/blob/master/wa_ipc.py ; https://github.com/patrickmmartin/winampremote/blob/master/source/API2.txt

### Cross-platform realism
- These DLLs depend on `HWND/HINSTANCE`, Win32 messaging, DirectSound/`out_ds`, and 32-bit C ABI. They **cannot be loaded natively** on macOS/Linux by a .NET/Avalonia process; a Windows-only loader or Wine is the only path to genuine binary-plugin compatibility.
- The Linux ''clones'' (XMMS, XMMS2, Qmmp, Audacious) have their **own** plugin APIs and implement Winamp *skins*, not the Winamp binary plugin interfaces; Audacious documents its plugin set independently. https://github.com/audacious-media-player/audacious-plugins ; https://www.musicpd.org/
- **Recommendation:** define a first-class managed `.NET` plugin API (input/DSP/output/general/visualization) with the same conceptual slots as Winamp, and optionally a Windows-only interop shim that maps `gen_/in_/out_/dsp_` entry points to your managed pipeline. Do **not** gate the product''s cross-platform story on binary plugin loading.

**What ''full compatibility'' historically meant:** the five classic plugin classes (gen_/in_/out_/dsp_/vis_) plus encoders, skins (.wsz/.wal), and language packs (.wlz). Winamp 5 achieved this by bolting the Winamp3/Wasabi modern-skin engine onto the Winamp 2 core via `gen_ff` (source: `Src/Plugins/General/gen_ff`).

---

## 7. Sonique and other visualization formats (optional)

### Sonique `.svp`
- A `.svp` is a **Windows DLL** exporting `QueryModule()` that returns a `VisInfo` struct (effect info + callbacks). Two versions distinguished by `VisInfo.Version` 0/1; v1 adds mouse click support and `Deinit`/`Clicked`/`ReceiveQueryInterface`. Effects consume waveform + spectrum buffers. Reference implementation/player: **Soniqueviewer** (SDL, MINGW) with `src/vis.h`. https://github.com/juanmv94/Soniqueviewer
- Sonique is long dead (Lycos, ~2001); `.svp` is x86-DLL-only and effectively non-portable. Treat as out of scope except via a Windows shim.

### Winamp AVS (`vis_avs`)
- Presets are `.avs` files describing a **tree of effect modules** (render/blur/trans/movement/etc. — dozens of `r_*.cpp` modules); native C++ plugins are `.ape` (via the bundled APE SDK). Source: https://github.com/manfromafar/winamp/tree/community/Src/Plugins/Visualization/vis_avs
- `.avs` is not a simple text format (structured/binary-ish); there are community tools that list a preset''s APE dependencies. https://github.com/grandchild/avs-preset-depends

### MilkDrop / MilkDrop 2 (`vis_milk2`)
- Presets are **text files** (`*.milk`) with an INI-like `[preset00]` section of named parameters (`zoom`, `rot`, `warp`, `wave_*`, `decay`, `echo_*`, `q1..q32`, etc.) plus **per-frame and per-vertex equations** in the NS-EEL2 expression language (`Src/ns-eel2`), and HLSL pixel/composite shaders in MilkDrop 2. https://web.archive.org/web/20240118132256/http://wiki.winamp.com/wiki/MilkDrop_Preset_Authoring ; https://github.com/manfromafar/winamp/tree/community/Src/Plugins/Visualization/vis_milk2
- Winamp ships MilkDrop2 presets under `Src/resources/data/Milkdrop2/presets/*.milk` (text). https://github.com/manfromafar/winamp/tree/community/Src/resources/data/Milkdrop2/presets
- Portability: projectM and Butterchurn read `.milk`; a cross-platform clone can implement the documented preset variables + an expression evaluator rather than porting the DX9 plugin.

**Clone takeaway:** support `.milk` (well-specified, portable) as the flagship visualizer; AVS is a stretch goal; Sonique `.svp` is Windows-only and optional.

---

## 8. Modern skin Maki — data format and tools

Maki = ''**M**ake **A** **K**iller **I**nterface'', the compiled scripting language for Wasabi/Winamp3/Winamp5 modern skins (XML + Maki ≈ HTML + JS). Source `.m` files are compiled by `mc.exe` (bundled: `Src/Wasabi/mc.exe`) to `.maki` bytecode. Modern skins ship as `.wal` (ZIP of XML + `.maki` + images); classic skins are `.wsz` (ZIP of fixed-position BMPs).
- Modern skins overview: https://web.archive.org/web/20240118132328/http://wiki.winamp.com/wiki/Modern_Skin:_Maki_Overview
- `.wal` structure and Maki semantics: https://jordaneldredge.com/webamp-modern

### Maki bytecode (fully reverse-engineered)
Documented by Jordan Eldredge (Webamp), which is the best available spec:
- Magic `FG` (initials of Francis Gastellu), u16 `1027`, u32 version (21/22/23).
- Sections in fixed order: Header → Types/Classes (GUIDs, variant-2 UUID encoding) → Methods → Variables → Strings → Bindings → Code (bytecode) → optional Debug filepaths → optional Debug line numbers.
- Opcode table (push/pop/mov/call/call-global/jump/add/…), stack/call-stack model, ''stack protection'' immediate after `call` (5.02/5.03+), and a later `0x70` ''strange call'' variant.
- Compiler flags (`/outpath`, `/debug`, `/debugfile`, `/dumpsym`) and a `debug.syn` dump format.
Sources: https://raw.githubusercontent.com/captbaritone/webamp/master/packages/webamp-modern/src/maki/maki-bytecode.md ; interpreter https://github.com/captbaritone/webamp/blob/master/packages/webamp-modern/src/maki/interpreter.ts

### Decompilers / tools
- **maki_decompiler** — the classic Maki disassembler/decompiler by **Ralf Engels** (Perl); independently archived. This is the tool Webamp''s author studied. https://web.archive.org/web/20180627114343/http://www.rengels.de/maki_decompiler/doc.html
- **Webamp Modern** Maki parser + JS interpreter (MIT) — practical reference implementation with a progress dashboard. https://github.com/captbaritone/webamp/tree/master/packages/webamp-modern ; demo https://webamp.org/modern/
- **qtWasabi** — GPL Qt reimplementation of the Wasabi engine aimed at running modern skins in a Winamp clone. https://github.com/qtWasabi/qtWasabi
- **`makdis` / `maki2xml`**: no corroborating source found; the known tools are `maki_decompiler` and the Webamp interpreter. `[UNVERIFIED — these specific tool names appear not to exist]`
- `mc.exe` (the official compiler) is closed-source and bundled as a binary; no compiler source is openly published. Define Maki support as an *interpreter* built from the documented bytecode (clean-room), not a recompiler.

---

## 9. Consolidated recommendations for the clone

1. **Formats to implement first (portable, well-specified):** M3U/M3U8 (BOM-aware, exact `#EXTINF` emission), PLS, B4S, XSPF, ML `playlists.xml`, iTunes plist import/export. Answer the ''EXTAUTOSTART'' question with ''not a Winamp extension''.
2. **Migration:** ship a read-only NDE importer (`main.dat`/`main.idx`) using the documented field IDs to bring over ratings/playcounts/lastplayed/ReplayGain; do not write NDE.
3. **Settings:** import `winamp.ini` (volume/EQ/preamp/limiter/skin/output/vis/title format), `paths.ini`, `winamp.q1` EQ presets, `winamp.bm` bookmarks.
4. **Features that carry the brand:** 10-band EQ (both algorithms + both band tables + limiter + auto-EQ), ReplayGain (album/track + preamp + −6 dB fallback + clip prevention), Jump-to-File with a real queue, ratings, smart-view query language and default smart views, playlists/bookmarks/history, podcasts, MTP/USB sync, .milk visualizer.
5. **Drop dead-tech explicitly:** AOD/AOL online services, ml_online/ml_fanzone/ml_nft, CD burning (Sonic), Gracenote auto-tag server, iPod iTunesDB/ActiveSync/NJB sync (unless libgpod is deliberately adopted).
6. **Plugin strategy:** own managed `.NET` plugin API; Winamp binary plugin compat is a Windows-only optional shim (and legally separate from the WCL-licensed reference source).
7. **Skins:** classic `.wsz` bitmap engine and modern `.wal` XML+Maki engine are distinct workstreams; Maki is tractable because the bytecode is documented (Webamp) and a decompiler exists (maki_decompiler).

---

## 10. Primary sources

- Winamp community source mirror (formats ground truth): https://github.com/manfromafar/winamp (alt mirror https://git.axenov.dev/Museum/winamp)
- Playlist implementations: `Src/playlist/*` — https://github.com/manfromafar/winamp/tree/community/Src/playlist
- NDE engine + schema: `Src/nde/*` and `Src/Plugins/Library/ml_local/*`
- Plugin headers: `Src/Winamp/{GEN.H,IN2.H,OUT.H,DSP.H,config.h,PathsINI.cpp}`
- NDE format spec: http://gutenberg.free.fr/fichiers/SDK%20Winamp/nde_specs_v1.txt (archived)
- NDE readers: https://github.com/Wiiseguy/node-nde , https://docs.rs/crate/xnde/latest , https://github.com/daniel15/ndephp , https://github.com/neuralys/nde
- Winamp developer wiki (archived): Nullsoft Database Engine, Modern Skin Maki Overview, MilkDrop Preset Authoring — https://web.archive.org/web/20240118132257/http://wiki.winamp.com/wiki/Nullsoft_Database_Engine , https://web.archive.org/web/20240118132328/http://wiki.winamp.com/wiki/Modern_Skin:_Maki_Overview , https://web.archive.org/web/20240118132256/http://wiki.winamp.com/wiki/MilkDrop_Preset_Authoring
- Maki bytecode + interpreter: https://github.com/captbaritone/webamp (packages/webamp-modern) ; background: https://jordaneldredge.com/webamp-modern ; original decompiler: https://web.archive.org/web/20180627114343/http://www.rengels.de/maki_decompiler/doc.html
- Winamp help (features/plugins/devices): https://pawelporwisz.pl/winamp/Help/Windows/help_windows_start.php , `...help_windows_preferences.php`, `...help_windows_library.php`
- EQ 5.51 changes: https://meggamusic.co.uk/winamp/docs/winamp_551_eq_changes.htm ; ReplayGain player requirements: https://replaygain.hydrogenaudio.org/player_preamp.html
- Backup/file inventory: https://diymediahome.org/backing-copying-winamp-media-library-database
- Skinning archives (classic/moderndoc PDFs): https://github.com/WACUP/Winamp-Skinning-Archive
- Sonique `.svp`: https://github.com/juanmv94/Soniqueviewer
- History/versions/license: https://en.wikipedia.org/wiki/Winamp ; https://www.theregister.com/software/2024/10/16/releasing-winamp-source-goes-badly-for-its-owners-anyway/417709 ; WACUP: https://getwacup.com/
