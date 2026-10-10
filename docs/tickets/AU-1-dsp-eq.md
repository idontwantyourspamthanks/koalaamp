# AU-1 · DSP pipeline + 10-band EQ

- **Milestone:** M1 · **Est:** 5d · **Depends:** FE-2, FE-4 · **Status:** unstarted

## Story
The audio identity of the player: a C++ float pipeline that applies ReplayGain, a Winamp-semantics 10-band EQ, preamp, and limiter to every sample, allocation-free, at ≤ 50ms latency — and the guard that makes sure libvlc's native EQ never fights ours on the callback path.

## Tasks
- [ ] Pipeline skeleton: deinterleave → stages → interleave → miniaudio sink; fixed 1024-frame blocks; pre-allocated buffers (steady-state heap diff = 0).
- [ ] ReplayGain stage: album/track modes, preamp, peak-aware clip prevention, −6 dB untagged fallback; R128 (LUFS→gain) tags.
- [ ] EQ stage: 10 peaking biquads, Winamp band table (60…16000 Hz); Winamp-curve and Constant-Q modes (`eq_type`); 0-64 UI values (31=0dB); coefficients double-precision, recomputed on change only.
- [ ] Preamp + soft limiter (1.5:1 knee at −0.1 dBFS), Winamp "limiter" flag semantics.
- [ ] **EQ-rules wiring:** in DSP mode, assert `libvlc_media_player_set_equalizer` is never called and `audio-filter` stays clear (FE-2's pinned test generalized into the engine); native EQ is reachable only in bit-perfect mode.
- [ ] `.eqf` parse/write (byte-compatible with Winamp presets) + `winamp.q1` import.
- [ ] Auto-load EQ per track (album-keyed store) + indicator hook.
- [ ] Golden tests: transfer-function sweeps per band/mode vs reference curves; pink-noise RG pairs; +12dB limiter scenario.

## Acceptance
- Transfer functions match reference curves within tolerance (golden fixtures, both curve modes, all 10 bands + preamp).
- `.eqf` round-trip is byte-identical on the fixture set; `winamp.q1` imports the classic preset set.
- Latency ≤ FE-2's measured budget (pipeline adds ≤ 10ms over raw passthrough).
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

## Notes / risks
- Biquad coefficient drift on parameter change: crossfade coefficients over one block (SE-5's acceptance requires glitch-free preset switching) — build the crossfade into the stage now, not later.
