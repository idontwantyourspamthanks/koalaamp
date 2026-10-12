# AU-2 spike — what a track boundary actually costs on the DSP path

**Question.** FE-4 owns the chain (`VlcPcmTap` → `SPSC ring` → `MiniAudioSink`)
and hands the next media to `setPrefetchHook` 10 s before the end, but the core
still stops and discards at the boundary. What exactly is lost there, and what
does a handover that loses nothing have to do?

**Harness.** `spikes/au2_gapless/` — two 4 s WAVs (A 440 Hz, B 660 Hz), the real
`VlcPcmTap` + `FloatRing` + `MiniAudioSink` (null backend, 1024-frame periods,
48 kHz stereo), a timeline of `framesIn`/`framesRendered`/ring fill/`silenceFrames`/
`flushes`/`discarded`, and a mode per candidate handover. Reproduce:

```bash
cmake --preset linux-gcc && cmake --build build/linux-gcc --target spike_au2
./build/linux-gcc/spikes/au2_gapless/spike_au2 s1                 # FE-4 baseline
./build/linux-gcc/spikes/au2_gapless/spike_au2 s2                 # swap AT EndReached
./build/linux-gcc/spikes/au2_gapless/spike_au2 late 67200 150     # shipped: tail-drain swap
./build/linux-gcc/spikes/au2_gapless/spike_au2 late 67200 100 flac # FLAC (deeper burst)
./build/linux-gcc/spikes/au2_gapless/spike_au2 s2 19200           # compact ring
```

## Findings

1. **libvlc hands over its decode-ahead in one burst as soon as a media starts.**
   At t+5 ms `framesIn` is already 1.15 s (WAV) / ~1.27 s (FLAC) while
   `framesRendered` is 0.04 s. **Consequence:** the ring's *capacity*, not the
   boundary code, decides how long the tail can be, and the incoming track's
   burst is what wants the space.

2. **The aout flush fires at end-of-input, and FE-2/FE-4 cleared the ring there.**
   Baseline (mode `s1` = stop + discard + set new media):

   ```
   ENDREACHED: fill=1.035s rendered=3.008s aIn=4.000s flushes=1 discarded=0.000s
   ...
   discarded(ring)=1.035s
   ```

   The FE-4 boundary threw away **1.035 s** of an ordinary 4 s track at every
   natural end — a jump cut, not a gap. A "did the sink starve?" metric misses
   it entirely: the incoming track's burst arrives within milliseconds and fills
   the hole with the *wrong* audio.

3. **Swapping at EndReached (`s2`) is worse, not better**: the outgoing tail and
   the incoming burst are resident together and do not fit —

   ```
   [s2] SUMMARY seam=881.3ms (42304 frames) ... aDropped=42144
   ```

4. **Swapping once the tail has drained (`late`, 150 ms) loses nothing** on the
   relaxed 1.365 s ring: `seam=0.0ms`, `aDropped=0`, `discarded(ring)=0`. The
   tail still in the ring covers the incoming media's activation; the freed
   space absorbs its burst. With FLAC the burst is deeper, so the tail budget has
   to give way — the shipped rule derives it from the engine's own measurement
   (`ringFrames − deepest fill seen`) with a 50 ms floor, and always keeps the
   floor over an exactly-fitting burst:

   | tail | container | seam | dropped |
   |---|---|---|---|
   | 150 ms | WAV | 0 ms | 0 |
   | 150 ms | FLAC | 21 ms | 1024 frames |
   | 100 ms | FLAC | 0 ms | 0 |
   | 50 ms | FLAC | 0 ms | 0 |

5. **The compact ring cannot be fixed by the boundary code.** `s2 19200` (the
   200 ms request → 341 ms actual): `aDropped=91136` frames = 1.9 s dropped over
   the run. The burst fits nowhere, so audio is discarded at *play*, at *seek*
   and at the *boundary* regardless of the handover.

## What AU-2 implemented

- `FloatRing` counts what `discardAll()` drops; `VlcPcmTap::cbFlush` no longer
  clears the ring (the core clears it only on a decided discontinuity: seek,
  stop, explicit switch).
- `PlaybackCore` arms the boundary on a natural end (`armHandover`) and swaps
  when the ring tail has drained to
  `min(gaplessTailMs, capacity − learned decode-ahead)`, floored at 50 ms —
  `swapToNext`, one decoder, one ring, no stop and no discard.
- `RingRegime::Relaxed` became the default (FE-4's deferred decision).
- Rejected: a second media player pre-opened at prefetch time (mode `s3`, the
  alternative the ticket's brief suggested) — it needs a second sink source, a
  second DSP instance on another audio thread and a cross-ring switch, and its
  best case is no better than the tail-drain swap.

## Residual / caveats

- The `42.7 ms` of `silenceFrames` at the top of every run is the *initial*
  activation (the sink starts before the first decode burst); it is not a
  boundary and is excluded where the tests measure from a settled baseline.
- Gapless here is sample-exact **per source**; codec-delay/padding trimming at
  the boundary itself is the muxer's business (WAV/FLAC are exact; a lossy
  source keeps whatever gapless metadata its decoder reports).
- The incoming track must be openable without network I/O for the swap to beat
  the tail; a streamed source (SV-2) can exceed it and needs its own buffering,
  which is why `gaplessTailMs` is configurable.
- The decode-ahead depth is learned from the engine (published as
  `PlaybackMetrics::decodeAheadFrames`), so a different libvlc build or codec
  moves the budget automatically instead of silently breaking the boundary.
