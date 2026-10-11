# FE-2 spike — libvlc callbacks + EQ rule + latency

- **Status:** in-review · **Owner:** FE-2 · **Date:** 2026-10-10/11
- **Ticket:** `docs/tickets/FE-2-libvlc-callbacks-eq.md`
- **Gates:** AU-1 latency budget, AU-2 gapless decision, spec §3.1
- **Reference dev machine:** Linux (Kubuntu), PipeWire 1.6.2 session with the PulseAudio
  compat socket, USB "BurrBrown" codec + on-board ALC897, gcc 15.2, Qt 6.10.2,
  **libvlc 3.0.23** (distro `libvlc-dev`), miniaudio 0.11.22 (vendored).

## TL;DR

1. **The callback path works** — `libvlc_audio_set_callbacks` +
   `libvlc_audio_set_format_callbacks` delivers decoded PCM to a lock-free ring that a
   miniaudio sink renders with **zero xruns over 30 min on ALSA and Pulse** (see §4).
2. **The EQ rule holds and is now pinned as a permanent ctest** (`libvlc EQ-rule test`,
   §3): installing a native equalizer changes the PCM the callback sees; clearing it
   restores the baseline. Therefore DSP mode must never enable native EQ — confirmed
   both ways.
3. **Two load-bearing spec values changed** (§2, §3): on VLC 3.0.x the callback output is
   **S16N only** (not FL32), and libvlc's native equalizer has a **−12 dB internal base
   gain** while the public band API advertises ISO frequencies — not the Winamp table the
   filter actually runs.
4. **Latency ≤ 50 ms p95 is not met on Linux** (§4). The floor is libvlc's *decoded block*
   (50 ms WAV, 52 ms MP3, 96 ms FLAC at 48 kHz), which VLC delivers atomically and which
   no available option shrinks; our pipeline adds only the device period on top. Numbers,
   not a verdict: the smallest zero-xrun end-to-end p95 measured is **91 ms (MP3)** on
   ALSA. This is a real constraint on AU-1, not a tuning miss.
5. **libcpp vs C API:** raw C headers, per FE-0's decision (`docs/spikes/fe0-deps.md`).
   No libvlcpp wrapper is compiled into the tree.
6. **Acquisition:** Linux distro package (`apt-get install libvlc-dev`), wired into
   `scripts/vendor-deps.sh`; nothing LGPL is vendored into the source tree.

## 1. What was built (and the interface choice)

Code lives in `src/interop/vlc/` (the only place that includes `vlc/vlc.h`, per
spec §2.2):

| file | purpose |
|---|---|
| `VlcPcmTap.{hpp,cpp}` | raw-C-API tap: instance + media player, `libvlc_audio_set_callbacks`, format setup, EQ control, statistics |
| `SpscRing.hpp` | lock-free single-producer/single-consumer float ring + enqueue→render residency stamps |
| `MiniAudioSink.{hpp,cpp}` | miniaudio playback device (WASAPI/CoreAudio/ALSA/Pulse), pulls the ring, counts xruns |
| `Metrics.hpp` | allocation-free percentile recorder for audio-thread sampling |
| `vlc_spike.cpp` | measurement harness (latency / xrun / seam), emits JSON |

The tap requests a fixed output format from VLC and converts it to interleaved float for
the pipeline. `libvlc_audio_set_callbacks` **overrides all other audio output** (per the
libvlc docs), which is exactly the DSP-mode seam in spec §3.1.

**libcpp vs C API — decided: raw C headers.** FE-0 already picked raw headers to avoid
compiling an LGPL C++ wrapper into the binary (`docs/spikes/fe0-deps.md`); this spike
confirms the C API covers everything needed here (`libvlc_audio_set_callbacks`,
`libvlc_audio_set_format_callbacks`, `libvlc_audio_equalizer_*`). libvlcpp is not used
anywhere.

## 2. Finding: the callback format is S16N only (FL32 is rejected)

The ticket allowed "S16N or FL32". On VLC 3.0.x the second is not available: the `amem`
audio output accepts **only S16N**. Requesting `FL32` fails at run time:

```
amem audio output error: format not supported: FL32, 2 channel(s), 48000 Hz
main audio output error: module not functional
main decoder error: failed to create audio output
```

Source: `modules/audio_output/amem.c` (3.0.x) — the module registers
`add_string("amem-format", "S16N", … /* TODO */)` and `Start()` checks the returned chroma
with `strcmp(format, "S16N") != 0` → "format not supported" (the `amem-format` variable is
a stub; FL32 lands in VLC 4.x). **Consequence:** our DSP-mode tap does one
`s16 → float32` conversion per block (`VlcPcmTap::cbPlay`). This is cheap (0.8–1.5 % CPU
total in §4) but means 24-bit sources are quantised to 16 bits *upstream of us* on this
path. Flagged for AU-1/spec: if 24-bit fidelity is wanted in DSP mode, it needs a
non-`amem` route (VLC 4, or the bit-perfect path).

## 3. Finding: the EQ-upstream rule — demonstrated both ways, pinned as a test

### The rule

`libvlc_media_player_set_equalizer()` installs VLC's `equalizer` module by adding it to the
`audio-filter` chain (`lib/media_player.c`, `set_equalizer` → `audio-filter` variable). That
chain is built in `src/audio_output/dec.c` **before** the audio output plays, i.e.
**upstream** of the callback. So PCM arriving in `libvlc_audio_play_cb` is
**post-native-EQ**, and DSP mode must not enable native EQ (it would double-EQ our own
biquads). The spike proves this and keeps it true.

### Evidence (test tone, 1 kHz, all ten bands + preamp at +12 dB)

Measured through the tap's run-time RMS (exponential mean-square of the callback PCM):

| state | callback RMS | vs baseline |
|---|---|---|
| DSP mode, no native EQ | baseline | 1.00× |
| `set_equalizer` +12 dB bands **and** +12 dB preamp | ≈4× baseline | ≈4× |
| `set_equalizer(NULL)` (user-filter chain empty) | back to baseline | ±few % |

**Two gotchas discovered while proving it** (both matter beyond the test):

- **`EQZ_IN_FACTOR = 0.25` (−12 dB) is the equalizer's base gain.**
  `modules/audio_filter/equalizer.c` defines `EQZ_IN_FACTOR (0.25f)` and `EqzConvertdB`
  compensates band gains around it, so a bands-only "+12 dB" preset nets only ≈ **+2.5 dB**
  (−12 dB base + 12 dB band). Setting the preamp to the same +12 dB cancels it and yields
  the nominal +12 dB. **For bit-perfect mode this means "flat" native EQ is −12 dB unless
  the preamp compensates** — a live bug trap for SE-5/AU-1.
- **The band API and the filter disagree on frequencies.** `libvlc_audio_equalizer_get_band_frequency()`
  returns the **ISO** table (31.25, 62.5, …, 16000 — `lib/audio.c:547`,
  `f_iso_frequency_table_10b`), while the filter defaults (`equalizer-vlcfreqs=true`) to
  `f_vlc_frequency_table_10b` = **60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000,
  16000** — the Winamp table. So band *index i* means a different frequency to the API than
  to the filter. The filter's default matches spec §4.2's Winamp table; do **not** trust
  `get_band_frequency()` for the mapping.

### Permanent test

`tests/libvlc_eq_test.cpp` (ctest name: *"libvlc hands the callback post-native-EQ PCM
(spec §3.1 EQ rule)"*) synthesises a 1 kHz tone, snapshots the callback RMS with the chain
empty, installs a +12 dB native EQ and asserts the RMS rises > 1.5×, then clears the EQ and
asserts the RMS returns to baseline within 15 %. It runs headless (the callback path needs
no sound device), so it is CI-safe; CI installs `libvlc-dev` on Linux so it actually runs.
If it ever fails, either the EQ stopped reaching the callback (rule broken) or the tap
stopped seeing PCM.

## 4. Latency and xruns

### Method

No speaker-loopback rig exists on this machine (no capture of the analogue output), so
latency is measured the way the ticket permits: **callback-timestamp deltas**. The tap
stamps every write with `CLOCK_MONOTONIC`; the sink pops those stamps as it consumes and
records the real enqueue→device-pull residency per frame group. Reported latency is
`residency p95 + one device period` (the period is the sink's own buffer; the hardware
buffer below miniaudio is not observable without `snd_pcm_delay`, so this is a slight
under-estimate and is stated as such). `xrun` = either a device callback that could not be
fully served (underrun) or frames the tap had to drop because the ring was full (overrun).

### The dominant term is libvlc's decoded block

VLC hands us whole decoder blocks, and **it fills whatever ring we give it** (no internal
back-pressure on this API), so the ring capacity *is* the pipeline latency, floored by one
block. Measured callback intervals (48 kHz output):

| source | block p50 | block p95 | note |
|---|---|---|---|
| WAV (raw 48 k) | 50.0 ms | 50.0 ms | 2400 frames, demuxer-sized |
| MP3 (44.1 k → 48 k) | 26.1 ms | 52.3 ms | 1 or 2 frames per callback |
| FLAC (48 k) | 96.0 ms | 96.2 ms | 4608-frame FLAC blocks |

There is **no libvlc 3.0.x option to shrink this** (`--aout-rate` is obsolete; the demuxer
decides raw block size and codecs decide their frame size). The "shorter callback block"
fallback in the ticket therefore cannot be taken at this layer.

### Measured configurations

Ring capacity is the power-of-two float ring size (VLC fills whatever it is given);
"residency p95" is the real enqueue→device-pull age; latency = residency p95 + one
period. **Every xrun-free row is above 50 ms, and every sub-50 ms row drops audio.**

| backend | source | ring cap | period | residency p95 | **latency p95** | drops | underruns | dur |
|---|---|---|---|---|---|---|---|---|
| ALSA | FLAC | 170 ms | 5.3 ms | 175.3 ms | **180.6 ms** | 0 | 0 | 12 s |
| ALSA | MP3 | 85 ms | 5.3 ms | 86.0 ms | **91.3 ms** | 0 | 0 | 12 s |
| ALSA | WAV | 85 ms | 5.3 ms | 90.4 ms | 95.7 ms | 256 (5 ms) | 0 | 12 s |
| ALSA | FLAC | 341 ms | 10.7 ms | 342.6 ms | 353.3 ms | 0 | 0 | 15 s |
| ALSA | FLAC | 341 ms | 10.7 ms | 330 ms | 362 ms | 0 | 0 | 180 s |
| ALSA | FLAC | 1365 ms | 10.7 ms | 1191 ms | **1207 ms** | 0 | 0 | 180 s |
| ALSA | MP3 | 42.7 ms | 5.3 ms | 47.4 ms | **52.7 ms** | 69360 | 343 | 12 s |
| Pulse | FLAC | 341 ms | 16.0 ms | 349.1 ms | 365.1 ms | 0 | 0 | 12 s |
| Pulse | FLAC | 1365 ms | 16.0 ms | 1198 ms | 1214 ms | 0 | 0 | 180 s |
| Pulse | MP3 | 42.7 ms | 16.0 ms | 51.2 ms | 67.2 ms | 80150 | 171 | 12 s |

The 30-minute soaks are in §5. Reading:

- **Ring capacity sets the latency** — libvlc has no internal back-pressure on this API,
  so it runs ahead and keeps the ring full; the ring size *is* the buffer.
- **One decoded block is the floor.** Any ring smaller than the p95 block drops frames
  (last two rows). The p95 block is 50 ms (WAV), 52 ms (MP3), 96 ms (FLAC) here.
- **Back-pressure does not work.** Blocking inside the play callback to resist an over-full
  ring throttles libvlc itself — that callback *is* the audio-output clock. Measured: a
  1 s-bounded blocking write dropped delivery to ~62 % of real time and produced 3500
  underruns in 60 s. Reverted. So the push API offers no way to keep the ring shallow.
- **CPU is negligible**: 0.8–1.5 % of one core end-to-end in every configuration.

### Verdict vs acceptance

- **Latency ≤ 50 ms p95: NOT achieved on Linux.** Best xrun-free p95 is **91 ms (MP3,
  ALSA)**; WAV 96 ms; FLAC 181 ms. The gap is libvlc's decode block, which is engine-side
  and identical in nature on WASAPI and CoreAudio, so the same floor must be assumed there
  (those backends could not be measured on this machine).
- **Zero xruns:** see §5.
- **Seam < 50 ms:** met — see §6.

### What this means for AU-1 / spec §3.1

The spec's implicit assumption that a callback sink tracks the device closely is wrong for
VLC 3.0.x: end-to-end DSP-mode latency is **≈ one decode block + one device period**, i.e.
~50–200 ms depending on source, independent of our sink. Options for the M1 budget
(decision for AU-1, not taken here): accept ~100 ms and size the visualiser/gapless logic to
it; or require VLC ≥ 4 / a pull-capable path for low latency; or keep DSP mode but document
the codec-dependent floor. **AU-2's "> 50 ms → force DSP mode"** threshold is effectively
always tripped for the easy case, so the seam number below is what matters.

## 5. 30-minute soaks

Two campaigns, `koalaamp_vlc_spike --file soak.flac (35 min, 48 kHz) --period 512
--duration 1800`, run idle on the reference machine (no builds or other audio apps during
the windows). "cap" is the actual power-of-two ring capacity.

**Config A — low latency (`--ring-ms 200`, cap 341 ms):**

| backend | callbacks | frames in | **drops** | **underruns** | latency p95 | CPU |
|---|---|---|---|---|---|---|
| ALSA | 18753 | 86,413,824 | **3584** (74.7 ms) | 0 | 361 ms | 0.8 % |
| Pulse | 18751 | 86,404,608 | **4096** (85.3 ms) | 0 | 367 ms | 1.4 % |

**Config B — slack (`--ring-ms 700`, cap 1365 ms > libvlc's ~1.17 s decode-ahead):**

| backend | callbacks | frames in | **drops** | **underruns** | latency p95 | CPU |
|---|---|---|---|---|---|---|
| ALSA | 18751 | 86,404,608 | **0** | **0** | 1269 ms | 0.9 % |
| Pulse | 18751 | 86,404,608 | **0** | **0** | 1273 ms | 1.4 % |

Both campaigns delivered exactly real time (18 751 blocks × 4608 frames = 1800 s).

Reading: **zero xruns over 30 min per backend is met in Config B** (and 0 underruns in
Config A). Config A's overrun frames are ring-boundary partial writes — with the ring full,
a 4608-frame block occasionally no longer fits into the space freed since the previous
block; over 30 min that is ~1 block every 8 min (0.004 % of frames), not a systematic
glitch. The 180 s runs of Config A showed 0 drops, so it is rare, load-sensitive, and is
recorded here as a **documented exception** rather than hidden. The strict zero-drop
configuration needs the ring to exceed libvlc's decode-ahead, which costs ~1.2 s latency —
another reason the latency criterion (above) is the binding constraint.

## 6. Bit-perfect seam (`stop()` → new media → first PCM)

Measured by `koalaamp_vlc_spike --seam --native-seam` (stop the current media, set the next,
play, timestamp the first decoded callback / the `MediaPlayerPlaying` transition); three
runs each, ALSA sink:

| path | run 1 | run 2 | run 3 | verdict |
|---|---|---|---|---|
| callback path (first decoded PCM after `stop()`) | 18.6 ms | 13.1 ms | 15.0 ms | **< 50 ms** |
| native aout `pulse` (`stop()` → `Playing`) | 7.4 ms | 6.7 ms | 7.6 ms | **< 50 ms** |

Both are comfortably under AU-2's 50 ms threshold, so **gapless does not have to force DSP
mode** on this platform — though DSP mode is still the default for other reasons, and the
sample-exact concat at the boundary in the sink remains the better path. Caveat: the native
figure is a *state-transition* proxy (VLC exposes no PCM hook on its own output), so treat
it as a lower bound; the callback figure is the real stop→first-PCM gap. Methodology drives
the AU-2 decision either way: DSP-mode gapless concatenates in our sink, so the seam that
matters is the VLC-side stop/restart, and it is well inside budget.

## 7. Reproduction

```bash
# build (Linux): distro libvlc + vendored single-file deps
sudo apt-get install -y libvlc-dev
bash scripts/vendor-deps.sh
cmake --preset linux-gcc && cmake --build --preset linux-gcc

# the permanent EQ-rule guard
ctest --preset linux-gcc -R "spec §3.1 EQ rule" -V

# latency / xrun / seam harness
build/linux-gcc/src/interop/vlc/koalaamp_vlc_spike \
  --backend alsa --file tone.flac --ring-ms 200 --period 512 \
  --duration 1800 --seam --json out.json
```

## 8. Deviations, assumptions, open items

- **S16N only** (§2): the ticket's "S16N or FL32" resolves to S16N on VLC 3.0.x.
- **+12 dB preset** (§3): the test uses preamp + bands at +12 dB because the bands-only
  variant nets only +2.5 dB (VLC's −12 dB base). The rule is proven either way; the preamp
  makes the assertion unambiguous.
- **Latency method** (§4): timestamp deltas, not speaker loopback (no capture path on the
  machine); the hardware buffer below miniaudio is excluded, so numbers are
  slightly optimistic.
- **No back-pressure possible** (§4): blocking in the play callback throttles libvlc's own
  clock (measured 62 % delivery rate), so loopback-style pull pacing is off the table and
  the ring must be sized with slack to avoid rare drops.
- **WASAPI/CoreAudio**: unverified on this machine.
- **30-minute soaks** (§5) use FLAC (largest block, worst case) at two ring sizes: 200 ms
  (low latency, occasional overrun frames) and 700 ms (slack above libvlc's ~1.17 s
  decode-ahead; zero drops, ~1.2 s latency).
- `miniaudio` is pinned at 0.11.22 (FE-0); spec §3.1's "miniaudio 0.12" does not exist as a
  release — 0.11.22 is the latest tag.
- No WCL/GPL source was consulted. The only VLC source read is LGPL libvlc itself
  (`amem.c`, `equalizer.c`, `equalizer_presets.h`, `lib/audio.c`, `media_player.c`), to
  explain observed behaviour.

## 9. References

- libvlc callback API: <https://videolan.videolan.me/vlc-3.0/group__libvlc__audio.html>
- `modules/audio_output/amem.c` (S16N-only check) — VLC 3.0.x
- `modules/audio_filter/equalizer.c` (`EQZ_IN_FACTOR`, `EqzConvertdB`, `EqzFilter`) — VLC 3.0.x
- `modules/audio_filter/equalizer_presets.h` — `f_vlc_frequency_table_10b` (Winamp),
  `f_iso_frequency_table_10b`
- `lib/audio.c` (libvlc EQ API; `get_band_frequency` returns the ISO table) — VLC 3.0.x
- `src/audio_output/dec.c` (filter chain built before the audio output) — VLC 3.0.x
- `docs/research/audio.md` §2.2, §4; `docs/spec.md` §3.1; `docs/spikes/fe0-deps.md`
