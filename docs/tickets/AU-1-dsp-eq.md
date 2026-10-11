# AU-1 · DSP pipeline + 10-band EQ

- **Milestone:** M1 · **Est:** 5d · **Depends:** FE-2, FE-4 · **Status:** in-review

## Story
The audio identity of the player: a C++ float pipeline that applies ReplayGain, a Winamp-semantics 10-band EQ, preamp, and limiter to every sample, allocation-free, at ≤ 50ms latency — and the guard that makes sure libvlc's native EQ never fights ours on the callback path.

## Tasks
- [x] **Sink ownership (binding, FE-4):** `PlaybackCore` owns the chain `tap → SPSC ring → miniaudio sink` — AU-1 does NOT build a second sink or re-plumb anything. The DSP is a *transform* installed at the existing seam: `IAudioEngine::setPcmCallback` (post-gain, pre-ring float blocks, `src/playback/PlaybackCore.hpp:27,95`). Stages run inside that callback: deinterleave → RG → EQ biquads → preamp/limiter → interleave → return; 1024-frame blocks; pre-allocated buffers (steady-state heap diff = 0). — `src/audio/DspPipeline.{hpp,cpp}`; installed with `DspPipeline::makeCallback()`.
- [x] ReplayGain stage: album/track modes, preamp, peak-aware clip prevention, −6 dB untagged fallback; R128 (LUFS→gain) tags. — `src/audio/ReplayGain.{hpp,cpp}`.
- [x] EQ stage: 10 peaking biquads, Winamp band table (60…16000 Hz); Winamp-curve and Constant-Q modes (`eq_type`); 0-64 UI values (31=0dB); coefficients double-precision, recomputed on change only. — `src/audio/Equalizer.*`, `EqValue.hpp`, `Biquad.hpp` (crossfade over one block).
- [x] Preamp + soft limiter (1.5:1 knee at −0.1 dBFS), Winamp "limiter" flag semantics. — `src/audio/Limiter.hpp`.
- [x] **EQ-rules wiring:** in DSP mode, assert `libvlc_media_player_set_equalizer` is never called and `audio-filter` stays clear (FE-2's pinned test generalized into the engine); native EQ is reachable only in bit-perfect mode. — `metrics().nativeEqEnabled` asserted on the DSP-on path (`tests/dsp_test.cpp` seam case) + the static guard extended to `src/audio`.
- [x] `.eqf` parse/write (byte-compatible with Winamp presets) + `winamp.q1` import. — `src/audio/EqPreset.{hpp,cpp}`.
- [x] Auto-load EQ per track (album-keyed store) + indicator hook. — `src/audio/AlbumEqStore.{hpp,cpp}`.
- [x] Golden tests: transfer-function sweeps per band/mode vs reference curves; pink-noise RG pairs; +12dB limiter scenario. — `tests/dsp_test.cpp`.

## Acceptance
- Transfer functions match reference curves within tolerance (golden fixtures, both curve modes, all 10 bands + preamp).
- `.eqf` round-trip is byte-identical on the fixture set; `winamp.q1` imports the classic preset set.
- Latency: pipeline adds ≤ 10ms over FE-2's measured passthrough (the absolute ≤50ms target was **not met by the engine itself** — libvlc's decoded block floors it at 50–96ms; see spec §3.1 measured constraints; AU-1's budget is the *delta*, not the total).
- Steady-state allocation-free (heap snapshot across 10k blocks identical).
- The no-native-EQ assertion holds across the full test suite (fails the build if violated).

## Out of scope
- Gapless/crossfade (AU-2).
- Tempo (SoundTouch) — Phase 2.
- The EQ *window UI* (SE-5 consumes this pipeline).
- Bit-perfect mode plumbing (engine-level; already spec'd §3.1).

## References
- `spec.md` §3.1 (EQ rule), §4.1-4.3 (chain, semantics, RG)
- VLC `equalizer_presets.h` — the verified band table cross-check
- `docs/research/audio.md` §3-4 — EQ semantics, biquad approach, RG modes
- `docs/tickets/FE-2-libvlc-callbacks-eq.md` — the upstream-EQ evidence this builds on
- `src/playback/PlaybackCore.hpp` + `docs/tickets/FE-4-playback-core.md` — **the seam you plug into** (setPcmCallback, post-gain pre-ring) and the core's threading contract

## Notes / risks
- Biquad coefficient drift on parameter change: crossfade coefficients over one block (SE-5's acceptance requires glitch-free preset switching) — build the crossfade into the stage now, not later.

## Outcome (2026-10-11, branch `au1`)

**Delivered.** `src/audio/` (`koalaamp_dsp`, Qt-free and libvlc-free): `Biquad.hpp`,
`EqValue.hpp`, `Equalizer.*`, `ReplayGain.*`, `Limiter.hpp`, `DspPipeline.*`,
`EqPreset.*`, `AlbumEqStore.*`. The transform is installed at the FE-4 seam with
`core.setPcmCallback(dsp.makeCallback(downstream))`; no second sink, no engine edits.

**Public surface:** `EqValue.hpp` exposes `eqValueToDb/eqDbToValue/bandQ`, the band table and
`EqMode`; `Equalizer`/`EqSettings`; `DspPipeline::setSettings/setReplayGain/makeCallback`;
`EqPreset` parse/write + `classicEqPresets()` + `importWinampQ1`; `AlbumEqStore`. SE-5's
sliders consume `eqValueToDb`/`EqSettings`.

**Golden method (recorded):** transfer functions are measured as steady-state sine response
through the real `DspPipeline` and compared to reference curves computed *independently* in
`tests/dsp_test.cpp` from the RBJ Audio-EQ-Cookbook magnitude response (not from the
production coefficient code), within 0.6 dB — all 10 bands ±12 dB, both modes; plus the
analytic property |H(f₀)| = requested gain at every centre frequency (0.25 dB) and a full
"Rock" preset vs the product of per-band references. ASAN+UBSAN clean; full suite 92/92
(72 baseline + 20 new).

**Deviations / decisions.**
- Sink-ownership seat: `PcmCb` is a `const float*` per spec §2.3, but the seam hands the
  transform the mutable pre-ring scratch. `DspPipeline::makeCallback` casts away the view's
  constness in one documented place (`mutableBlock`, well-defined — the object is not const)
  rather than changing the pinned interface and breaking SE-2.
- Slider range is Winamp's 6-bit field **0..63** (the ticket's "0-64" inclusive-end); 31 = 0 dB,
  0 = +12 dB, 63 = −12 dB. `.eqf` bytes store that same value directly.
- Curve modes: `eq_type=0` (Winamp 4Front) = proportional Q, ~1 octave at unity narrowing to
  ~1/3 octave at ±12 dB; `eq_type=1` (Constant-Q) = fixed Q ≈ 1.414. Both golden-tested; the
  numeric law is KoalaAmp's documented interpretation (the spec pins the modes, not the law).
- The EQ/master preamp is applied **after** the biquads, per spec §4.1 / task order.
- Limiter is a memoryless soft-knee 1.5:1 curve at −0.1 dBFS with a hard 0 dBFS ceiling
  (deterministic; a hard ceiling is what makes "+12 dB never passes full scale" hold).
- Latency delta is 0 frames: the stage is a per-block in-place transform (no lookahead). FE-2's
  decode-block floor is untouched. The compact ring's ~0.87 s play/seek drop is libvlc's
  decode-ahead (accepted, VLC-side) and is unchanged by the DSP stage.

**No WCL/GPL source was consulted for this change.** The `.eqf`/`q1` container layout is
attributed to the MIT winamp-eqf reference (Webamp) in `EqPreset.hpp` and `LICENSES.md`.
