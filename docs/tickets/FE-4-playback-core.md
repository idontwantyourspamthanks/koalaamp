# FE-4 · Play an MP3 end-to-end

- **Milestone:** M1 (M0's deferred tail; blocks SE-2, SE-3, AU-1, VZ-1, LI-1) · **Est:** 2d · **Depends:** FE-2 · **Status:** in-review

## Story
The app-level playback core: file → queue → libvlc → our DSP-tap path → miniaudio sink, with transport (play/pause/stop/next/prev), volume, seek, and end-of-track auto-advance — a headless `PlaybackCore` that every M1 UI ticket (SE-2's transport buttons, posbar, volume slider) can drive. FE-2 proved the engine mechanics in a spike; this ticket turns them into the app's playback service.

## REUSE, DON'T REIMPLEMENT
FE-2's `src/interop/vlc/{VlcPcmTap,SpscRing,MiniAudioSink,Metrics}` are the proven engine primitives (callback path, ring, sink) and FE-2's spike doc holds the binding constraints (S16N-only amem; EQ-upstream rule — never enable native EQ on this path; ring regimes 200ms/700ms per spec §3.1 measured constraints). Wrap, don't re-derive.

## Tasks
- [x] `PlaybackCore` in `src/playback/` implementing the `IAudioEngine` seam: play(media), pause/resume, stop, seek(ms), volume(0-100 perceptual), next/prev, queue-append (deque semantics: queue consumed before shuffle-refill; JTFE semantics arrive with SE-3). — `src/playback/PlaybackCore.{hpp,cpp}`; queue ops, repeat (off/all/one) + shuffle (session-list refill), and `previous()`'s Winamp restart-if->3 s rule included.
- [x] Wire: `VlcPcmTap` → `SpscRing` → `MiniAudioSink` (the FE-2 chain) with the compact ring (200ms) as default; config for the relaxed ring per spec §3.1. — `PlaybackConfig::RingRegime{Compact,Relaxed}` → 19200/67200 float requests → 341 ms/1365 ms actual caps (asserted). No new ring code: the FE-2 primitives are used as-is.
- [x] EndReached → advance (queue order; repeat/shuffle flags honored — state machine minimal, UI comes later). — VLC-thread events are marshalled through a lock-free mailbox tagged with a **media generation** (see findings: a real EndReached here does *not* satisfy `positionMs ≈ lengthMs`, so the staleness test must be the generation).
- [x] Position/volume/state change notifications (callback or signal abstraction — NO Qt in src/playback; it stays UI-agnostic behind an observer interface; src/ui adapts it later). — `IPlaybackObserver` (Qt-free, no signals); all callouts on the core's dispatch thread. Zero Qt includes in `src/playback/`, enforced by a grep test.
- [x] Media lifecycle discipline: libvlc media/player release on track change — 200 track changes leak-free under ASAN (the FE-2 spike measured RSS flat; reproduce at the core level). — `VlcPcmTap::setMedia*` keeps its release discipline; the soak below runs 200 changes with LeakSanitizer on.
- [x] Headless smoke binary `spikes/fe4_playback/` (or a test): play N seconds of a generated WAV (write a tiny sine WAV in-test — no fixture fetch needed), assert PCM flowed (tap got samples), position advanced, seek lands, volume applies, EndReached fires with an empty queue (stops) and with a queued track (advances). — `tests/playback_core_test.cpp` (12 Catch2 cases, `[fe4]`); no spike binary — the tests are the smoke harness and run in ctest.
- [x] Gapless scaffolding only: double-buffer seam (prefetch next media) — the actual sample-exact boundary is AU-2; here it's the mechanism placeholder (documented). — `setPrefetchHook`/`prefetchArmed`/`onPrefetch`: the next media is resolved `prefetchLeadMs` (10 s default) before the end and handed to the hook; the core still stops/discards at the boundary, which is exactly what AU-2's double buffer replaces.

## Acceptance
- Test suite: WAV/MP3/FLAC/OGG play through the full chain (generated WAV mandatory; the compressed formats via small committed fixtures with clear provenance or generated-then-encoded if ffmpeg is present — record which); transport+seek+volume+EndReached all assert from the tap data.
- 200 track changes: ASAN clean, RSS stable (±10MB) — run locally, report numbers.
- `src/playback/` contains zero Qt includes (enforced by a grep test).
- No native EQ enabled anywhere on the path (the FE-2 ctest rule now guards the core too).

## Outcome (2026-10-11)

**What landed**

| file | change |
|---|---|
| `src/playback/PlaybackCore.{hpp,cpp}` | the service: transport, queue/repeat/shuffle, volume, seek, EndReached advance, prefetch seam, observer fan-out; Qt-free (pimpl — no interop/vlc header in the public header) |
| `src/playback/IPlaybackObserver.hpp` | observer seam + `PlaybackState`/`RepeatMode`; every callout is on the core's dispatch thread |
| `src/playback/CMakeLists.txt` | `koalaamp_playback_core` (STATIC, links `koalaamp_vlc`), gated on `KOALAAMP_HAS_LIBVLC`; the header-only `koalaamp_playback` interface target stays |
| `src/CMakeLists.txt` | interop/ is added before playback/ — it decides the gate above |
| `src/interop/vlc/VlcPcmTap.{hpp,cpp}` | FE-4 seams on the FE-2 primitive: media-player event listener (each event tagged with the media generation), decoded-PCM observer (the §3.1 DSP insertion point), output gain (the volume stage), `resume()`, `lengthMs()`, locator media, null guards |
| `src/interop/vlc/MiniAudioSink.{hpp,cpp}` | `SinkBackend::Null` — the real device thread + data callback with no sound hardware (headless/CI); ALSA/Pulse unchanged |
| `tests/playback_core_test.cpp` (+ `tests/CMakeLists.txt`) | 12 Catch2 cases, `[fe4]`; each is its own ctest entry (dedicated process) |

*Threading*: the owner-thread API posts commands to the core's dispatch thread, which owns the state machine and is the only thread that calls observers; libvlc events arrive on VLC threads and only push into a 64-slot lock-free mailbox (drops counted in `metrics().eventsDropped`); `IAudioEngine::setPcmCallback` is the documented exception that runs on the audio-output thread. The threading contract is spelled out in `PlaybackCore.hpp`.

**Evidence**

```bash
cmake --preset linux-gcc && cmake --build --preset linux-gcc   # clean, 0 warnings (core is -Wall -Wextra -Wpedantic -Wshadow)
ctest --preset linux-gcc                                       # 100% tests passed, 0 failed out of 72 (was 60; +12 FE-4)
ctest --preset linux-gcc -R "PlaybackCore|src/playback"        # 12/12
```

- **Formats** — WAV synthesised in-test (`writeSineWav`, no fetch); **MP3/FLAC/OGG generated-then-encoded in-test with the system `ffmpeg`** (present on this machine, 7.x) — no binary fixtures committed, so no third-party audio provenance. Each format asserts PCM flowed (`framesDecoded > 0`), `lengthMs > 0`, RMS > 0.1, backend `Null`, no native EQ, sink consumed the ring (`framesRendered > 0`), state `Playing`, and (MP3/FLAC) `positionMs` advancing. OGG/Vorbis reports ~0 ms from `libvlc_media_player_get_time()` on this engine while the tap demonstrably delivers PCM; the test documents and skips only that clock assertion.
- **Transport** (8 s WAV) — position advances from the tap clock, pause holds it to within 60 ms, resume advances, `togglePause` round-trips, stop → `positionMs() == 0`; observer callouts all on one thread that is neither the test's nor a libvlc thread.
- **Seek** (8 s WAV) — `seekMs(3000)` lands (≥2800 ms), re-decodes (framesDecoded jumps by ~64 800 immediately after) and keeps advancing.
- **Volume** — baseline RMS ≈ 0.35 at 100; 25 → < 0.15× (and > 0.03×, i.e. quieter not muted); 0 → < 0.01×; back to 100 → > 0.7×; `setVolume(0.5f)` → 50; `setVolume(-3)` clamps to 0. Gain is applied in the tap's float stage (see finding c).
- **EndReached** — empty queue: fires once and stops (`positionMs()==0`, state `Stopped`). Queued: advances **exactly one** step (0.6 s A → 8 s B, C stays queued) — the regression that pins the stale-event guard.
- **Prefetch seam** — the hook fires once with the queued media and `prefetchArmed()` latches; the queued media still starts on the natural end (the seam does not alter the audio path — that is AU-2).
- **Ring regimes** — compact = 16 384 frames (341 ms), relaxed = 65 536 frames (1365 ms); on a 1 s track the compact ring drops frames and the relaxed one drops none.
- **Guards** — a test greps all of `src/playback/` for Qt includes, `vlc/vlc.h` and the libvlc EQ API (none); a runtime case asserts `nativeEqEnabled == false` across a full transport + media-change cycle. The FE-2 EQ-rule ctest still passes, including under ASAN.
- **Soak** — 200 track changes (4 media cycled; every third change goes through queue+`next()`), each waiting for the track-change callout **and** ≥100 ms of freshly decoded PCM:

  | metric | value |
  |---|---|
  | track changes / decoded frames | 200 / 4 924 800 |
  | `eventsDropped` / errors | 0 / 0 |
  | heap in use (`mallinfo2`) | 13 521 → 13 533 KiB (**+12 KiB** over the last 100 changes) |
  | RSS | 6 MiB cold → 19 MiB after the 100-change warm-up → 19 MiB at 200; **+4 KiB** over the last 100 changes |

  A separate 800-change sweep after the warm-up kept RSS flat at 45 064 → 45 096 KiB with `uordblks` oscillating without trend — no per-change growth over 4× the ticket's scale.
- **ASAN + UBSAN** (`-fsanitize=address,undefined -fno-sanitize-recover=undefined`): all 12 cases pass; LeakSanitizer reports **no leaks** (its output contains only its own internal `*dlerror*` suppressions) and there are no UBSAN diagnostics across the transport/seek/volume/format/EndReached cases and the 200-change soak.

**Findings (engine; for AU-1/AU-2 and an FE-2 doc addendum)**

Measured on this reference machine with the compact ring unless stated; each was reproduced with the FE-2 `VlcPcmTap`+`MiniAudioSink` pair directly (no `PlaybackCore` involved), and the overrun numbers also on PulseAudio:

1. **One overrun burst per media activation.** libvlc has no back-pressure on this API: it hands over its decode-ahead (~1.15 s) as fast as the ring drains, so a ring smaller than that drops the excess once per play **and once per seek** — compact: 41 000–41 800 frames (≈0.87 s) dropped; relaxed (1.365 s cap): 0. FE-2's soak measured drops only after a 2 s warm-up, so this one-time burst fell outside every reported window (its "3584 frames over 30 min" is steady state). **With the ticket's compact default, DSP-mode playback therefore loses ~0.9 s at the start of each track and after each seek.** Flagged for AU-1/AU-2 to revisit the default regime — not changed unilaterally here.
2. **EndReached and the position clock arrive ~1 s before the audible end.** Once all frames are handed over, the input finishes while the ring still holds audio; `get_time()` stalls 1.0–1.15 s short of `lengthMs()` and `MediaPlayerEndReached` is emitted at that point (5 s WAV → ~4.0 s; 0.6 s WAV → ~130 ms). Consequences: (i) an end/staleness test must **not** compare position to length — the core uses the tap's per-media **generation tag** instead; (ii) the auto-advance boundary is not sample-exact, which is what AU-2's double buffer has to fix.
3. **Volume belongs in the tap's float stage**, the one place every decoded sample passes as float; the core sets a gain there (one multiply per sample, no allocation) and maps the 0–100 control onto it. The squared-law curve is a placeholder AU-1 owns (spec §3.3).
4. **RSS is a weak leak signal on this stack.** Roughly one run in three shows a single ~25 MiB RSS step somewhere in the first ~400 activations while `mallinfo2` shows a flat live heap (~14 MiB `uordblks`, no arena/mmap growth) — i.e. the step is *not* allocated memory (consistent with libvlc's per-input threads leaving stack mappings resident). After it, RSS is flat for hundreds of changes. LeakSanitizer is therefore the leak gate; the test reports the exact RSS numbers and bounds them loosely.

## Deviations
1. **No `spikes/fe4_playback/` binary** — the ticket allows "(or a test)"; the 12 headless Catch2 cases are the smoke harness, run by ctest and under ASAN.
2. **Soak RSS bound is 32 MiB, not ±10 MiB** — with the exact numbers reported instead (finding 4). The ±10 MiB bound *is* met in dedicated-process runs where the one-time step falls outside the window (measured +4 KiB); it is not a meaningful assertion when the step lands inside.
3. **Compressed fixtures are generated in-test with ffmpeg** (the ticket's second option) rather than committed; the test skips those cases when ffmpeg is absent.
4. **The compact ring stays the default** per the ticket, even though finding 1 shows it drops ~0.9 s per track start/seek; changing the default is AU-1/AU-2's call, and this ticket reports the measurement rather than re-deciding it.
5. `VlcPcmTap`/`MiniAudioSink` were **extended** (event/gain/PCM-observer seams, null backend) rather than wrapped opaquely — the REUSE mandate is about not re-deriving the callback path, ring or sink, all of which are used exactly as measured in FE-2.


## Out of scope
- Any UI (SE-2+). DSP content beyond passthrough (AU-1). Gapless boundary (AU-2). Playlist persistence (PL-1). Jump-to-file queue semantics (SE-3).
- WASAPI/CoreAudio measurement (owner item from FE-2).

## References
- `docs/tickets/FE-2-libvlc-callbacks-eq.md` + `docs/spikes/libvlc-callbacks.md` — engine mechanics, S16N, EQ rule, ring regimes
- `src/interop/vlc/` — the primitives to wrap
- `docs/spec.md` §2.3, §3.1-3.3 — interfaces, PCM modes, volume semantics
- `AGENTS.md` — ASAN requirement for the audio path

## Notes / risks
- libvlc event loop: EndReached arrives on a VLC thread — marshal to the core's own dispatch, never call out to observers from VLC's thread (document the threading contract in the header). Implemented with a fixed-capacity mailbox on the tap's event listener; the drain runs on the core's dispatch thread. Each event carries the tap's **media generation** so a late event from a media the core already advanced past cannot skip a track — necessary because finding 2 makes `positionMs ≈ lengthMs` unusable as an end/staleness test.
- 200-change soak under ASAN is the point of this ticket — the FE-2 RSS-flat result was on a single track. Done: LSan clean over 200 changes; see Outcome/finding 4 for why RSS is reported rather than tightly asserted.
- Follow-ups this ticket opens (not fixed here): (a) ring-regime default — AU-1/AU-2 (finding 1: the compact default drops ~0.9 s per track start/seek); (b) sample-exact gapless boundary — AU-2 attaches to `setPrefetchHook`/the prefetch seam; (c) the final volume curve — AU-1; (d) an FE-2 spike-doc addendum for findings 1–2 (its soak methodology measured drops only after a 2 s warm-up, which hid the one-time burst).
- For SE-2: link `koalaamp_playback_core`, implement `IPlaybackObserver` (Qt signals adapt 1:1), drive `setVolume100`/`seekMs` from the widgets, and use `metrics()` for the diagnostics counters. `PlaybackCore` is deliberately not linked into `koalaamp` yet — no UI consumes it.
- `koalaamp_playback_core` only exists where libvlc + vendored miniaudio are present (the same gate as the FE-2 targets); on a machine without them the app builds but has no playback service.
