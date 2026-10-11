# FE-4 · Play an MP3 end-to-end

- **Milestone:** M1 (M0's deferred tail; blocks SE-2, SE-3, AU-1, VZ-1, LI-1) · **Est:** 2d · **Depends:** FE-2 · **Status:** unstarted

## Story
The app-level playback core: file → queue → libvlc → our DSP-tap path → miniaudio sink, with transport (play/pause/stop/next/prev), volume, seek, and end-of-track auto-advance — a headless `PlaybackCore` that every M1 UI ticket (SE-2's transport buttons, posbar, volume slider) can drive. FE-2 proved the engine mechanics in a spike; this ticket turns them into the app's playback service.

## REUSE, DON'T REIMPLEMENT
FE-2's `src/interop/vlc/{VlcPcmTap,SpscRing,MiniAudioSink,Metrics}` are the proven engine primitives (callback path, ring, sink) and FE-2's spike doc holds the binding constraints (S16N-only amem; EQ-upstream rule — never enable native EQ on this path; ring regimes 200ms/700ms per spec §3.1 measured constraints). Wrap, don't re-derive.

## Tasks
- [ ] `PlaybackCore` in `src/playback/` implementing the `IAudioEngine` seam: play(media), pause/resume, stop, seek(ms), volume(0-100 perceptual), next/prev, queue-append (deque semantics: queue consumed before shuffle-refill; JTFE semantics arrive with SE-3).
- [ ] Wire: `VlcPcmTap` → `SpscRing` → `MiniAudioSink` (the FE-2 chain) with the compact ring (200ms) as default; config for the relaxed ring per spec §3.1.
- [ ] EndReached → advance (queue order; repeat/shuffle flags honored — state machine minimal, UI comes later).
- [ ] Position/volume/state change notifications (callback or signal abstraction — NO Qt in src/playback; it stays UI-agnostic behind an observer interface; src/ui adapts it later).
- [ ] Media lifecycle discipline: libvlc media/player release on track change — 200 track changes leak-free under ASAN (the FE-2 spike measured RSS flat; reproduce at the core level).
- [ ] Headless smoke binary `spikes/fe4_playback/` (or a test): play N seconds of a generated WAV (write a tiny sine WAV in-test — no fixture fetch needed), assert PCM flowed (tap got samples), position advanced, seek lands, volume applies, EndReached fires with an empty queue (stops) and with a queued track (advances).
- [ ] Gapless scaffolding only: double-buffer seam (prefetch next media) — the actual sample-exact boundary is AU-2; here it's the mechanism placeholder (documented).

## Acceptance
- Test suite: WAV/MP3/FLAC/OGG play through the full chain (generated WAV mandatory; the compressed formats via small committed fixtures with clear provenance or generated-then-encoded if ffmpeg is present — record which); transport+seek+volume+EndReached all assert from the tap data.
- 200 track changes: ASAN clean, RSS stable (±10MB) — run locally, report numbers.
- `src/playback/` contains zero Qt includes (enforced by a grep test).
- No native EQ enabled anywhere on the path (the FE-2 ctest rule now guards the core too).

## Out of scope
- Any UI (SE-2+). DSP content beyond passthrough (AU-1). Gapless boundary (AU-2). Playlist persistence (PL-1). Jump-to-file queue semantics (SE-3).
- WASAPI/CoreAudio measurement (owner item from FE-2).

## References
- `docs/tickets/FE-2-libvlc-callbacks-eq.md` + `docs/spikes/libvlc-callbacks.md` — engine mechanics, S16N, EQ rule, ring regimes
- `src/interop/vlc/` — the primitives to wrap
- `docs/spec.md` §2.3, §3.1-3.3 — interfaces, PCM modes, volume semantics
- `AGENTS.md` — ASAN requirement for the audio path

## Notes / risks
- libvlc event loop: EndReached arrives on a VLC thread — marshal to the core's own dispatch, never call out to observers from VLC's thread (document the threading contract in the header).
- 200-change soak under ASAN is the point of this ticket — the FE-2 RSS-flat result was on a single track.
