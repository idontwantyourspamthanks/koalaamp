# FE-2 · Spike: libvlc callbacks + EQ rule + latency

- **Milestone:** M0 · **Est:** 3d · **Depends:** FE-0 · **Status:** unstarted

## Story
Prove the audio architecture's two load-bearing assumptions with measurements, not faith: (1) the libvlc audio-callback path (vmem/amem) delivers decoded PCM to us with acceptable latency and zero glitches through a miniaudio sink; (2) the **EQ-runs-upstream rule** — the user-filter chain, including `equalizer`, processes PCM *before* our callback, so DSP mode must never enable native EQ. This spike's numbers gate AU-1's latency budget and the gapless-mode decision.

## Tasks
- [ ] libvlcpp test binary: open a local FLAC + MP3, `libvlc_audio_set_callbacks` + `set_format` (S16N or FL32, 44.1k/48k) → lock-free ring buffer.
- [ ] miniaudio playback device (WASAPI shared / CoreAudio / ALSA; Pulse for the PipeWire-session case) pulling from the ring buffer.
- [ ] Measure: end-to-end p95 latency (callback → speaker, estimate via loopback or callback-timestamp deltas), xrun count over 30 min per backend, CPU %.
- [ ] **EQ rule verification:** play a test tone; snapshot 5s of callback PCM; call `libvlc_media_player_set_equalizer` with a +12 dB preset; snapshot again; assert the PCM RMS changed. Then set `audio-filter` to `""` and assert it returns to baseline. Record in the report.
- [ ] Add the same assertion as a permanent unit/integration test (this is the spec §3.1 rule's guard).
- [ ] Measure bit-perfect seam: `stop()` → new media → first callback, on the native-aout path; record the gap (drives AU-2's "< 50ms or force DSP mode" decision).
- [ ] Write `docs/spikes/libvlc-callbacks.md`.

## Acceptance
- Latency ≤ 50ms p95 on WASAPI-shared, CoreAudio, ALSA (Pulse informational).
- Zero xruns in 30 min per backend (allow documented exceptions for Pulse-under-load, if any).
- EQ-upstream behavior **demonstrated both ways** (EQ on changes callback PCM; cleared restores it) and pinned as a repeatable test.
- Seam measurement recorded with a number, not an impression.

## Out of scope
- DSP pipeline content (AU-1) — this spike moves raw PCM only.
- Gapless *implementation* (AU-2) — we only measure the seam.
- ReplayGain, trackers, network streams (FE-5 covers server streams).

## References
- `spec.md` §3.1 — PCM modes + the critical EQ rule (with VLC source citations)
- VLC source verified 2026-10-10: `lib/media_player.c:2214-2244` (`set_equalizer` → `audio-filter` var), `modules/audio_output/amem.c` (registers as normal `audio output` capability), `src/audio_output/dec.c:417-425` (filter pipeline built before aout play) — chain runs upstream of the callback.
- `docs/research/audio.md` §2.2, §4 — callback API, PCM tap analysis
- koalapaw `jellyfin-api` — n/a here, but the port (FE-5) will reuse this spike's stream plumbing.

## Notes / risks
- libvlcpp C++ wrapper vs raw C API: either is fine, but pick one for the whole codebase here and note it (interop consistency).
- If latency exceeds budget on any backend, the fallback is a shorter callback block / larger ring — try before declaring failure; the numbers matter more than the verdict.
