# AU-2 · Gapless + ReplayGain

- **Milestone:** M1 · **Est:** 4d · **Depends:** AU-1, FE-4 · **Status:** in-review

## Story
The seam between two tracks stops being audible and ReplayGain stops being dead
code. On the DSP path the boundary is ours — the sink owns the ring, libvlc
decodes into it — so a track change concatenates the outgoing track's last
sample with the incoming track's first one, and the incoming track's
ReplayGain/applied gain comes from its real tags instead of nothing.

## Tasks
- [x] Scheduler prefetch 10 s (spec §3.2). FE-4's seam is kept as the
      *resolution* step (`setPrefetchHook`/`onPrefetch`); AU-2 adds the boundary
      itself (`PlaybackConfig::gapless`, `gaplessTailMs`). — `PlaybackCore`.
- [x] Sample-exact boundary on the DSP path: at the natural end the core hands
      the *same* decoder the next media while the outgoing track's ring tail
      drains to `gaplessTailMs`, so the incoming track's first frame lands
      immediately after the outgoing track's last one. Nothing is stopped,
      nothing is discarded. — `PlaybackCore::advance/armHandover/swapToNext`.
- [x] The ring is no longer cleared from the aout thread. The flush callback
      fires *both* on a seek and at end-of-input, and clearing there threw away
      the last ~1 s of every track (measured) — the dominant boundary defect.
      Discards now happen only where the core decides a discontinuity (seek,
      stop, explicit track switch). — `VlcPcmTap::cbFlush`, `FloatRing::discardAll`.
- [x] Ring regime: the compact 200 ms ring cannot hold libvlc's decode-ahead
      burst (~1.15 s measured; FE-4 measured 0.87–1.15 s), so every
      play/seek/boundary drops ~0.9 s of audio. `RingRegime::Relaxed` is now the
      default (FE-4's finding 1 deferred exactly this decision); `Compact` stays
      selectable and is pinned as not gapless-capable by a test.
- [x] RG modes + preamp + clip prevention + −6 dB fallback + R128 tags: AU-1's
      stage, now driven by real tags. Activation seam:
      `PlaybackCore::setTrackPrepareHook` fires on the dispatch thread *before*
      the media's first block is decoded (both on a full activation and on a
      gapless handover), so the transform latches the right gain for the track's
      first block. `DspPipeline::prepare` no longer marks pending settings
      applied (that swallowed the tags pushed before the first block).
- [x] Minimal clean-room RG tag reader: `audio/RgTags.{hpp,cpp}` — Vorbis
      comments (FLAC, Ogg-Vorbis, Ogg-Opus) and ID3v2.2/2.3/2.4 TXXX frames,
      with `REPLAYGAIN_*`/`R128_*` keys and the Q7.8 integer form, + a bounded
      `RgTagSource` cache. Tags: `[au2][dsp][rgtags]` (synthetic byte buffers +
      ffmpeg-encoded real files).
- [x] Measurement + evidence: `tests/gapless_rg_test.cpp` (`[au2][core]`) —
      23-boundary sample-exactness, a 39-boundary tagged soak, the compact-ring
      caveat, the seek discard, repeat-one looping, RG activation (tagged vs
      untagged Δ), modes/preamp/fallback/clip, hook ordering; plus
      `spikes/au2_gapless/` (the probe that chose the design).
- [x] Lifetime fix the boundary work exposed: the PCM seam is invoked from
      libvlc's audio-output thread, so releasing a core mid-playback could let an
      in-flight callback touch the tap's scratch as it was freed. The destructor
      now closes the seam, waits (bounded) for an in-flight callback, stops the
      player and detaches the observer before releasing the tap; the header
      states the "the transform must outlive the core" contract.

## Acceptance
- **Concert-rip seam < 20 ms (DSP)** — met with margin: 23 consecutive
  boundaries on a 24-track concatenated sine, **0 frames** of silence, **0**
  frames discarded, **0** frames dropped, and the rendered PCM is *sample-exact*:
  every one of the 2 764 800 samples equals the concatenation of the source
  tracks (`mismatched samples=0`). `ctest -R koalaamp_au2`.
- **Bit-perfect seam < 50 ms** — FE-2's measured 6.7–18.6 ms callback/native-aout
  seam stands (unchanged; AU-2 does not touch that path).
- **RG ±6 dB tags measure ±0.5 dB applied** — met: a FLAC tagged `-3.00 dB`
  measures `-3.00 dB` (±0.5) against an independently generated reference sine,
  an untagged FLAC applies the `-6.00 dB` fallback, and the tagged-vs-untagged Δ
  is `+3.0 dB` (±0.5). Modes/preamp/fallback/clip asserted through the real
  control surface.
- **No clipping with prevent on** — met: with `preventClip` the peak-aware clamp
  holds the output ≤ 1.0 (`peakAbs(run->pcm) <= 1.0 + 1e-6`); with it off the
  same setting overdrives (`> 1.0`), so the guard is what held the level.
- **ASAN + UBSAN clean on the boundary path under a multi-track soak** — see the
  Outcome (40-track tagged soak, 39 gapless boundaries, LeakSanitizer on).

## Out of scope
- Crossfade (`config_fade`, 0-12 s) — spec §3.2 lists it as optional; not here.
- Tempo/SoundTouch, bit-perfect mode plumbing, server-stream RG (SV-2).
- TagLib integration / the full tag pipeline (LI-1): `RgTags` is the documented
  stand-in behind the same seam.
- The UI wiring of the prepare hook (`src/ui` is SE-4's; AU-2 publishes the seam
  and demonstrates it in tests).

## References
- `docs/spec.md` §3.1 (measured constraints, the compact-ring burst), §3.2
  (gapless design), §4.1/§4.3 (chain, RG semantics)
- `docs/tickets.md` AU-2 (tasks/acceptance)
- `docs/tickets/FE-4-playback-core.md` — findings 1–2 (the burst, the early
  EndReached) and the deferrals this ticket closes
- `docs/tickets/AU-1-dsp-eq.md` — the DSP stage + seam this drives
- `docs/spikes/au2-gapless.md` — the probe behind the design
- `src/playback/PlaybackCore.{hpp,cpp}`, `src/audio/ReplayGain.*`, `RgTags.*`
- `src/interop/vlc/VlcPcmTap.cpp`, `SpscRing.hpp` — the flush/discard semantics

## Notes / risks
- **The boundary is a two-condition problem.** At EndReached the decoder has
  handed over its whole decode-ahead, so the ring holds ~1 s of the outgoing
  track; if the incoming media is swapped *there*, its own ~1.15 s burst has
  nowhere to go and is dropped (measured). Waiting for the tail to drain frees
  the space, and the remaining tail covers the activation latency. The kept tail
  is derived per boundary from the engine's own measurement —
  `min(gaplessTailMs, ring capacity − deepest fill seen)`, floored at 50 ms —
  so a deeper-bursting codec automatically keeps less tail instead of dropping
  the incoming burst (measured: WAV ~1.15 s, FLAC ~1.27 s decode-ahead on the
  1.365 s relaxed ring, which leaves ~95–215 ms of budget).
- **Never cut a track short to satisfy a timer.** The tail-drain trigger only
  ever runs after a natural `EndReached`; a seek/next/play is a discontinuity
  and goes through the FE-4 stop/activate path, which discards the stale ring.
- **Compact ring caveat (spec §3.1):** with the 200 ms request the burst cannot
  be absorbed anywhere, so ~0.9 s is lost at every play/seek/boundary no matter
  what the boundary code does. That is why the default moved to relaxed and why
  a test pins `framesDropped > 0` for compact.
- A second media player was probed as an alternative (`spikes/au2_gapless` modes
  s2/s3) and rejected: it needs a second sink source, a second DSP instance on
  the audio thread, and a cross-ring switch, for a worse worst case.

## Outcome

**What landed**

| file | change |
|---|---|
| `src/playback/PlaybackCore.{hpp,cpp}` | gapless boundary (`PlaybackConfig::gapless/gaplessTailMs`, `armHandover`/`maybeHandover`/`swapToNext`), `setTrackPrepareHook` (the ReplayGain activation seam), explicit ring discards on seek/stop/switch, `Relaxed` ring default, seam-quiesce teardown, new metrics |
| `src/interop/vlc/VlcPcmTap.cpp` | the aout flush no longer clears the ring (documented at the call site) |
| `src/interop/vlc/SpscRing.hpp` | `discarded()` counter (what a discontinuity threw away) |
| `src/audio/DspPipeline.cpp` | `prepare()` no longer marks pending settings applied (they were swallowed before a track's first block) |
| `src/audio/RgTags.{hpp,cpp}` | the minimal clean-room RG/R128 tag reader + `RgTagSource` cache (the LI-1 seam) |
| `tests/gapless_rg_test.cpp` | `koalaamp_au2_tests`, 8 cases, `[au2][core]` — each its own ctest entry |
| `tests/dsp_test.cpp` | `[au2][dsp][rgtags]` — 7 cases: synthetic containers, malformed input, ffmpeg-written real files |
| `spikes/au2_gapless/` | the probe that chose the boundary design; `docs/spikes/au2-gapless.md` records it |
| `docs/tickets.md`, `docs/tickets/AU-2-gapless-replaygain.md` | index + this brief |

**Evidence**

```bash
cmake --preset linux-gcc && cmake --build --preset linux-gcc          # 0 new warnings
ctest --preset linux-gcc                                             # 100% tests passed, 0 failed out of 142
ctest --preset linux-gcc -R koalaamp_au2                             # AU-2 suite
./build/linux-gcc/tests/koalaamp_dsp_tests "[au2]" -s                # RgTags suite
# ASAN + UBSAN (the audio-path gate)
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
cmake --build build/asan --target koalaamp_au2_tests koalaamp_playback_tests koalaamp_dsp_tests
./build/asan/tests/koalaamp_au2_tests
```

- **Boundary (DSP path, relaxed ring):** 24 tracks / **23 boundaries** on one
  continuous 440 Hz sine split into tracks that each continue the previous
  phase — `mismatched samples=0` over 2 764 800 samples, `longestZeroRunFrames=1`,
  `framesDiscarded` and `framesDropped` unchanged from the settled baseline, no
  new `silenceFrames`. The rendered PCM *is* the concatenation of the sources:
  the seam is **0 frames (< 20 ms by the whole margin)**.
- **Soak:** 40 tagged FLAC tracks / **39 boundaries** with the real DSP doing
  work (RG + a +3.5 dB EQ band): 0 discarded, 0 dropped, 0 new silence, peak
  ≤ 1.0, final applied gain = the last track's tag. Same numbers under ASAN/UBSAN.
- **Compact ring caveat (spec §3.1):** pinned by a test — `ringFrames ≤ 16384`
  (341 ms) and `framesDropped > 24 000` frames (> 0.5 s) for a 1.2 s track: the
  ~1.15 s decode burst does not fit, so audio is lost at play/seek/boundary
  regardless of the boundary code. That is why the default is now `Relaxed`.
- **Discontinuity discipline:** a seek mid-ring discards the stale tail
  (`framesDiscarded` jumps by 54 720 frames = 1.14 s) and playback continues from
  the new position; the gapless path never discards.
- **ReplayGain activation:** a FLAC tagged `REPLAYGAIN_TRACK_GAIN=-3.00 dB`
  measures `-3.00 dB` ±0.5 against an independently generated reference sine; an
  untagged FLAC applies the `-6.00 dB` fallback; tagged-vs-untagged Δ = `+3.0 dB`
  ±0.5. Modes (off/track/album), preamp, album→track→fallback resolution,
  peak-aware clip prevention (≤ 1.0 with it on, > 1.0 with it off) and the
  per-track re-activation all assert through the real control surface.
- **Tags:** 7 cases over synthetic FLAC/Ogg-Vorbis/Ogg-Opus/ID3v2.3/v2.4 buffers
  (incl. UTF-16 descriptions, header and per-frame unsynchronisation, a
  malformed-length corpus) plus ffmpeg-written FLAC/Ogg/MP3 files whose values
  were verified byte-for-byte.

**Deviations / decisions.**
- **`RingRegime::Relaxed` is now the default.** FE-4 pinned `Compact` before
  gapless existed and explicitly deferred the decision to AU-1/AU-2 (its finding
  1); AU-1 left it. With the compact ring the boundary cannot be gapless (the
  burst fits nowhere), so the gapless-capable regime is the default and compact
  stays selectable — the spec's latency *selector* is untouched, only its
  default.
- **The boundary trigger is the ring tail, not EndReached.** Swapping at
  EndReached (the obvious reading of "prefetch then swap") measurably loses the
  incoming burst: the outgoing tail and the incoming burst cannot be resident
  together. The trigger is derived from the engine's own measurement
  (`capacity − deepest fill seen`), floored at 50 ms, and can only ever fire
  after a natural `EndReached`.
- **The flush no longer clears the ring** (`VlcPcmTap::cbFlush`). That call fired
  at end-of-input too, throwing away the last ~1.0 s of every track; the core now
  discards where it decides a discontinuity. This is the single largest boundary
  defect the ticket fixed.
- **A second media player was rejected** (the alternative the brief sketched):
  it needs a second sink source, a second DSP instance on another audio thread
  and a cross-ring switch, and its best case is no better. The probe
  (`spikes/au2_gapless`, mode `s3`) holds the measurement.
- **The app wiring of `setTrackPrepareHook` is not in this diff**: `src/ui` is
  SE-4's concurrent scope. The seam is published, documented and exercised by
  the tests (the same pattern AU-1 used for `setPcmCallback`); SE-2's
  `koalaamp_ui_playback` bridge is where the two-line registration belongs.
- **Escape hatch:** `PlaybackConfig::gapless = false` restores the FE-4
  stop/discard behaviour, so the boundary can be reverted without a revert.
- **Two lifetime rules the boundary work exposed and now enforces** (both were
  latent use-after-frees that the ASAN/UBSAN gate caught): (a) the object behind
  `setPcmCallback` must outlive the core — the seam runs on libvlc's audio-output
  thread and is handed the tap's own scratch, so the core's destructor now closes
  the seam and waits (bounded) for an in-flight callback before releasing the
  tap; (b) an `IPlaybackObserver` must outlive the core (or be removed first),
  because the dispatch thread keeps calling out until the destructor joins it —
  FE-4's tests declared observers after the core and are fixed to match.

**No WCL/GPL source was consulted for this change.** The tag reader is written
from the public FLAC/Ogg/Vorbis-comment/OpusTags/ID3v2 format documentation; the
`.eqf`/`q1` container layout (AU-1) keeps its existing Webamp attribution.
