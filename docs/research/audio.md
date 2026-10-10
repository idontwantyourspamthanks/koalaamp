AUDIO/PLAYBACK ENGINE RESEARCH FOR A MULTI-PLATFORM AVALONIA WINAMP-LIKE PLAYER
State of the art 2024-2026. Every factual claim is followed by a URL. Uncertain items are marked [UNVERIFIED].

=====================================================================
0. BOTTOM LINE / RECOMMENDATION
=====================================================================
- Primary engine: libvlc 3.0.x through LibVLCSharp. Rationale: it is the only mainstream option that natively provides BOTH (a) a Winamp-frequency 10-band EQ with preamp and (b) a decoded-PCM callback tap for visualizers, and it has an official, actively maintained .NET binding with first-class Avalonia support.
  - https://videolan.videolan.me/vlc-3.0/group__libvlc__audio.html
  - https://raw.githubusercontent.com/videolan/vlc/master/modules/audio_filter/equalizer_presets.h
  - https://github.com/videolan/libvlcsharp/blob/3.x/NEWS
- Secondary/alternative engine: libmpv (mpv 0.41.0), buildable LGPL (nothing forces GPL if built with -Dgpl=false), excellent output-device handling (WASAPI exclusive, CoreAudio exclusive, PipeWire) and native ReplayGain - but NO supported PCM callback (its audio-callback PR was rejected), so custom visualizers must jump through hoops. Good for a scriptable/embeddable engine, weaker for a MilkDrop-first app.
  - https://raw.githubusercontent.com/mpv-player/mpv/master/Copyright
  - https://github.com/mpv-player/mpv/pull/5566
- GStreamer is a credible third engine (LGPL, Windows/macOS binaries, clean tee->appsink PCM tap) but needs more C# plumbing and its 10-band EQ is ISO-frequency based, not Winamp-based.
- Decoders: rely on FFmpeg inside the engine for FLAC/MP3/Vorbis/Opus/AAC(+HE-AAC)/ALAC/WAV/WavPack/APE/Musepack. Add libopenmpt (BSD-3-Clause) for MOD/XM/IT/S3M and other tracker formats, which FFmpeg cannot play.
- Metadata: TagLib# 2.3.0 (LGPL-2.1) as the safe default; watch the MIT clean-room TagLibSharp2 (v0.6.0, 2026-01-05) as a modern replacement.
- Library search: SQLite FTS5 (adequate and simplest for 100k tracks); Lucene.NET still has no stable release.

=====================================================================
1. DECODER / FORMAT COVERAGE
=====================================================================
If the engine is FFmpeg-based (mpv, VLC, GStreamer all bundle/use FFmpeg or equivalent), the following are covered by in-tree LGPL decoders/demuxers:
- FLAC, MP3, Ogg Vorbis, Ogg Opus, AAC (LC + HE-AAC v1/v2 incl. SBR/PS), ALAC, PCM/WAV, WavPack, Monkey's Audio (APE), Musepack (MPC) - FFmpeg native codecs. Source: FFmpeg codecs documentation lists flac, aac, alac, wavpack, ape, mpc7/mpc8 decoders; most are native and LGPL:
  - https://ffmpeg.org/ffmpeg-codecs.html
  - https://ffmpeg.org/doxygen/trunk/md_LICENSE.html (most files LGPL-2.1+; some MIT/BSD)
- 24/32-bit hi-res and float PCM: handled internally by FFmpeg (s16/s32/fltp sample formats); final bit depth is determined by the audio output/backend, not the decoder. See mpv --audio-format / --audio-samplerate:
  - https://raw.githubusercontent.com/mpv-player/mpv/master/DOCS/man/options.rst (lines ~2364, --audio-samplerate; --audio-format)
- HE-AAC: FFmpeg native AAC decoder supports SBR/PS (HE-AAC v1/v2). [INFERENCE from FFmpeg codec listing; no single page states HE-AAC explicitly]

Tracker formats (MOD/XM/IT/S3M/MOD-family) are NOT supported by FFmpeg. Use libopenmpt:
- libopenmpt 0.8.0 stable released 2025-05-31 (0.8.9+release is the current doc build); cross-platform C/C++ library; per-format additions in 0.8 (PumaTracker, FTM, FC, GMC, RTM, etc.).
  - https://lib.openmpt.org/libopenmpt/2025/05/31/release-0.8.0
  - https://github.com/OpenMPT/openmpt (README)
- License: BSD-3-Clause (per package metadata / distro copyright):
  - https://github.com/openkylin/libopenmpt (License: BSD-3-Clause)
  - https://lib.openmpt.org/libopenmpt/license/
- No official .NET binding; ship native libs + P/Invoke. openmpt123 and an in-process API (openmpt_module_read_float_interleaved) make PCM extraction trivial.

CUE sheets:
- ffmpeg does NOT parse CUE. mpv has a CUE demuxer (changelog entry 'demux/cue: fix crash when there are no tracks'):
  - https://github.com/mpv-player/mpv/releases/tag/v0.41.0
- VLC also supports CUE. For accurate album-image splitting/preservation and AccurateRip/CTDB, CUETools.NET (C#, GPL) contains a full CUESheet parser + AccurateRip implementation:
  - https://github.com/gchudov/cuetools.net
- libcue (C) is a small BSD parser: https://github.com/lipnitsk/libcue

Gapless:
- Encoder delay/padding is signalled in the LAME tag (MP3) and iTunSMPB (AAC/M4A); Opus uses pre-skip. mpv/FFmpeg/VLC all consume these for gapless. Reference write-up with measurements:
  - https://www.lukaswojcik.com/blog/en/music-production/gapless-playback-encoder-delay-padding-mp3-aac-opus-flac-measured/
- NLayer (NAudio-adjacent) implements LAME/iTunSMPB trimming: https://github.com/naudio/NLayer/commit/184d7067ac9d0f4e6010e34ddf8ff25a9720c74b
- mpv gaplesness caveat issue: https://github.com/mpv-player/mpv/issues/2276

ReplayGain:
- mpv has native support: --replaygain=no|track|album, --replaygain-preamp, --replaygain-clip, --replaygain-fallback:
  - https://raw.githubusercontent.com/mpv-player/mpv/master/DOCS/man/options.rst (lines ~2199-2216)
- Spec: ReplayGain 2.0 specification (Hydrogenaudio):
  - https://wiki.hydrogenaudio.org/index.php?title=ReplayGain_2.0_specification
- Reference implementations: r128gain (https://github.com/desbma/r128gain), loudgain (https://wiki.hydrogenaudio.org/index.php?title=Loudgain). Both are ReplayGain-2 / EBU R128 based.

=====================================================================
2. CANDIDATE BACKENDS
=====================================================================

2.1 libmpv (mpv)
- Latest: mpv 0.41.0 (released 21 Dec; current in Arch as 1:0.41.0-6). Requires FFmpeg >= 6.1 and libplacebo >= 6.338.2.
  - https://github.com/mpv-player/mpv/releases/tag/v0.41.0
  - https://archlinux.org/packages/extra/x86_64/mpv
- License: mpv as a whole is GPLv2+ by default; it can be built as LGPLv2.1+ (license text) with -Dgpl=false, which is explicitly 'the intended use ... with libmpv.' GPL-only files that get disabled include ao_jack, ao_oss, x11 video out, dvdnav, cdda, etc. Linked FFmpeg build license also matters (do not link a GPL FFmpeg).
  - https://raw.githubusercontent.com/mpv-player/mpv/master/Copyright
  - https://github.com/mpv-player/mpv/issues/2033 (LGPL relicensing history)
  - LGPL build request context: https://github.com/shinchiro/mpv-winbuild-cmake/issues/586
- Platform coverage: Windows/macOS/Linux (and Android/iOS builds exist). AO drivers: wasapi (with --audio-exclusive), coreaudio / coreaudio_exclusive (macOS), pipewire, pulse, alsa, sdl, jack (GPL), pcm, null, sndio.
  - https://raw.githubusercontent.com/mpv-player/mpv/master/DOCS/man/ao.rst
- EQ: no native graphic EQ. Use --af=lavfi=[equalizer]/firequalizer/superequalizer or a custom filter; tempo via scaletempo2 (default when --audio-pitch-correction is on), rubberband, or lavfi-tempo.
  - https://raw.githubusercontent.com/mpv-player/mpv/master/DOCS/man/af.rst
  - https://blog.fraggod.net/2018/04/12/mpv-audio-visualization.html
- PCM tap for visualization: NO supported callback. The audio-callback driver PR #5566 (mpv_audio_callback) was closed unmerged in 2019:
  - https://github.com/mpv-player/mpv/pull/5566
  - https://github.com/mpv-player/mpv/issues/9283 (libmpv: get audio frame?)
  Workarounds: (a) --ao=pcm --ao-pcm-file=<fifo> --ao-pcm-waveheader=no writes raw PCM to a file/FIFO (timing/realtime caveats): https://raw.githubusercontent.com/mpv-player/mpv/master/DOCS/man/ao.rst ; (b) render viz inside mpv with --lavfi-complex="[aid1]asplit[ao][a];[a]showcqt[vo]" (does not feed app code).
- .NET binding maturity: no official binding; community Rust (libmpv-rs, LGPL-2.1: https://lib.rs/crates/libmpv) and Dart (mpv_audio_kit) exist; expect to write your own P/Invoke around mpv_create/mpv_set_option_string/mpv_command, or use a small third-party wrapper. [UNVERIFIED for a production-grade maintained C# libmpv wrapper]

2.2 libvlc / LibVLCSharp
- VLC stable line: 3.0.x (3.0.21 in 2024; bugfixes continue). VLC 4.0 remains in development [UNVERIFIED stable date]. libvlc 3.0.x is the API LibVLCSharp targets.
  - https://www.videolan.org/vlc/releases/3.0.21.html
  - https://www-test.videolan.org/news/2024-06-10-vlc-3-0-21/
- License: LGPL-2.1+ for libvlc headers/API.
  - https://raw.githubusercontent.com/videolan/vlc-3.0/master/include/vlc/libvlc_media_player.h (LGPL-2.1 header)
- LibVLCSharp: official VideoLAN binding; latest 3.10.1 (NEWS). Traits: Avalonia support (fixed in 3.8.0 for Avalonia 11; 3.9.7 adds Avalonia 12 compat), AOT support (3.9.7), .NET 6/8/9/10 targets, macOS/tvOS targets, MAUI/Uno/WinUI/WPF/WinForms/GTK/Uno. NuGet native packages: VideoLAN.LibVLC.Windows/Mac/Android/iOS/tvOS.
  - https://github.com/videolan/libvlcsharp/blob/3.x/NEWS
  - https://github.com/videolan/libvlcsharp/blob/3.x/README.md
  - NuGet: https://www.nuget.org/packages/LibVLCSharp/
- Native 10-band EQ with preamp and Winamp-identical frequencies. libvlc_audio_equalizer_* API: get_band_count (10), get_band_frequency, set_preamp/get_preamp, set_amp_at_index/get_amp_at_index, new_from_preset, and libvlc_media_player_set_equalizer.
  - https://videolan.videolan.me/vlc-3.0/group__libvlc__audio.html
  - Band frequencies (equalizer_presets.h): 60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000 Hz - exactly Winamp's 10 bands; 18 named presets (flat, classical, club, dance, fullbass, fullbasstreble, fulltreble, headphones, largehall, live, party, pop, reggae, rock, ska, soft, softrock, techno).
    https://raw.githubusercontent.com/videolan/vlc/master/modules/audio_filter/equalizer_presets.h
  - Implementation: modules/audio_filter/equalizer.c (band-pass IIR filter bank): https://github.com/videolan/vlc/blob/master/modules/audio_filter/equalizer.c
  - Equalizer test/example: https://github.com/videolan/vlc/blob/master/test/libvlc/equalizer.c
- PCM tap: libvlc_audio_set_callbacks() + libvlc_audio_set_format()/libvlc_audio_set_format_callbacks(), with libvlc_audio_play_cb / pause / resume / flush / drain / cleanup / volume callbacks. This lets the app receive decoded PCM and own the output device (a first-class visualization tap).
  - https://raw.githubusercontent.com/videolan/vlc-3.0/master/include/vlc/libvlc_media_player.h (audio callbacks at ~line 617-770)
- Output: libvlc picks its own aout modules (Windows mmdevice/wasapi/directsound, macOS coreaudio, Linux pulse/pipewire/alsa) selectable via libvlc_audio_output_set / --aout.

2.3 GStreamer
- Version/vitality: 1.28.0 released 2026-01-27; latest bugfix 1.28.8 on 2026-10-08. Very active.
  - https://gstreamer.freedesktop.org/releases/1.28/
  - https://gstreamer.freedesktop.org/releases/1.26/
- License: core and official plugins are LGPL-2.1; GPL plugins (when linking GPL libs) are intended to be split into a separate package. This makes GStreamer safely embeddable in a closed app provided you avoid the GPL-plugin set.
  - https://gstreamer.freedesktop.org/documentation/plugin-development/appendix/licensing-advisory.html
  - https://github.com/GStreamer/gst-docs/blob/master/markdown/legal-information.md
- Binaries: Windows MSVC runtime+devel MSIs (and MinGW); macOS packages via cerbero (.pkg/framework); Linux distro packages. Windows deployment docs:
  - https://gstreamer.freedesktop.org/documentation/installing/on-windows.html
  - https://github.com/GStreamer/gst-docs/blob/master/markdown/deploying/windows.md
  - https://gstreamer.freedesktop.org/documentation/deploying/mac-osx.html
- EQ: equalizer-10bands (10 bands from 29 Hz to 15.011 kHz, gain -24..+12 dB) - ISO/log spacing, NOT Winamp frequencies; add a preamp via volume/audioamplify.
  - https://gstreamer.freedesktop.org/documentation/equalizer/equalizer-10bands.html
- PCM tap / visualization: tee element to branch the pipeline, then audioconvert -> audio/x-raw,F32LE -> appsink for raw PCM; or use the built-in spectrum element for FFT magnitudes. Built-in audiovisualizers (spectrascope, etc.) exist in gst-plugins-bad.
  - https://gstreamer.freedesktop.org/documentation/coreelements/tee.html
  - https://gstreamer.freedesktop.org/documentation/app/appsink.html
  - https://gstreamer.freedesktop.org/documentation/audiovisualizers/spectrascope.html
  - https://github.com/GStreamer/gst-plugins-bad/blob/master/gst/audiovisualizers/README
- .NET binding maturity: gir.core (GirCore.Gst-1.0) is the modern generated binding (releases 0.7.0/0.7.0-preview.2/0.8.0). GStreamerSharp is the older hand-written binding. Expect more manual caps/refcount handling than LibVLCSharp.
  - https://github.com/gircore/gir.core/releases
  - https://www.nuget.org/profiles/GirCore

2.4 BASS (Un4seen)
- License: proprietary, closed source. Free for non-commercial/individual use; commercial use requires a paid license. The BASS.NET wrapper and ManagedBass are separate wrappers, not the engine.
  - http://www.un4seen.com/bass.html
  - https://www.un4seen.com/license.php?l=1
  - https://www.radio42.com/bass/bass_purchase.html
  - https://github.com/ManagedBass/ManagedBass (MIT wrapper)
- Platform coverage: Windows, macOS, Linux, Android, iOS, plus OS-specific add-ons. [UNVERIFIED exact current version; BASS 2.4.x line historically]
- Strengths: trivial PCM/FFT access for visualizations (BASS_ChannelGetData with BASS_DATA_FFT), pluggable codecs/add-ons, low-latency. Equalizer typically via the BASS_FX peaking-EQ filters rather than a built-in Winamp EQ. [UNVERIFIED that BASS ships Winamp-frequency EQ presets]
- Verdict: fastest path to a visualizer-heavy player, but non-open-source and license-encumbered for commercial distribution; it cannot be bundled like LGPL libs.

2.5 FFmpeg APIs directly
- License: LGPL-2.1+ by default; enabling --enable-gpl (e.g. x264/x265) makes the build GPL. Decode-only use can stay LGPL.
  - https://ffmpeg.org/doxygen/trunk/md_LICENSE.html
- Version: FFmpeg 8.0 (announced Aug 2025); 7.x line widely deployed. Auto-builds: https://github.com/BtbN/FFmpeg-Builds
  - https://ffmpeg.org/pipermail/ffmpeg-devel/2025-August/347886.html
  - https://ffmpeg.org/download.html
- Verdict: maximal control (own AVIO, own DSP, own output) but the most engineering effort; you re-implement playback, gapless, device management, EQ, and visualization tap yourself. Usually the wrong first choice when libvlc/GStreamer exist.

2.6 NAudio
- Version/vitality: NAudio 3.0.0 (2026-08-15) and 3.1.0 (2026-09-07). Major 3.0 change: the monolithic assembly split into packages, minimum TFM net9.0, core becomes cross-platform and Native-AOT compatible, and new subsystems land: cross-platform effects suite, ALSA backend, libsndfile backend, VST3 hosting, and a (preview) macOS wrapper package NAudio.MacOS.
  - https://raw.githubusercontent.com/naudio/NAudio/main/RELEASE_NOTES.md
  - https://github.com/naudio/NAudio
- Historically Windows-only, now partially cross-platform; Windows (WASAPI exclusive, Media Foundation, ACM, DMO, ASIO, XAudio2) remains the deepest path. macOS support ships pre-release (X.Y.Z-preview.N).
- Relevant for DSP (BiQuadFilter -> 10-band EQ) and for a Windows-only bit-perfect WASAPI-exclusive path.

2.7 CSCore
- Windows-only (MS-PL); Linux/macOS only on an 'experimental' netstandard branch. Repo appears effectively dormant (2.4k stars, 481 commits; no recent release activity evident). Since NAudio 3 now covers most of the same ground cross-platform, CSCore is not recommended for a new project.
  - https://github.com/filoe/cscore

=====================================================================
3. WINAMP 10-BAND EQ SEMANTICS + HOW TO GET IT PER BACKEND
=====================================================================
Winamp semantics: 10 fixed bands at 60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000 Hz, each gain adjustable (Winamp GUI range approx -12..+12 dB, stored as 0..64 with 31 = 0 dB in .EQF/.q1 files), plus a preamp gain. Presets live in Winamp.q1 / *.eqf; the format is documented and widely mirrored.
  - Original presets + JSON of Hz info: https://github.com/schollz/Winamp-Original-Presets
  - XMMS/Winamp preset import notes: https://wiki.daveking.com/index.php/XMMS_Equalizer_Presets_from_WinAmp
  - Audacious preset import history: https://redmine.audacious-media-player.org/issues/1078
- Native support:
  - libvlc: YES - libvlc_audio_equalizer_* with exactly those 10 frequencies + preamp (see section 2.2). Presets are Winamp-named. This is the single biggest reason to prefer libvlc.
  - mpv: NO native graphic EQ - use --af=lavfi=[equalizer=f=...:t=q:w=...:g=...] per band, or firequalizer/superequalizer, or implement biquads in .NET.
  - GStreamer: equalizer-10bands exists but uses ISO frequencies (29Hz..15kHz), not Winamp's; a Winamp-faithful EQ must be built from 10 peaking biquads (or 10 bands via `equalizer` elements with explicit frequencies).
  - BASS: typically BASS_FX peaking EQ filters; [UNVERIFIED Winamp presets].
  - Custom .NET: build 10 peaking biquads from the RBJ Audio-EQ-Cookbook; NAudio exposes BiQuadFilter.PeakingEQ(freq, Q, gainDb). NWaves (MIT) also implements biquads.
    - NAudio BiQuadFilter: https://github.com/naudio/NAudio (NAudio.Dsp.BiQuadFilter)
    - NWaves: https://github.com/ar1st0crat/NWaves ; filters wiki: https://github.com/ar1st0crat/NWaves/wiki/Filters
Chaining: EQ is applied as a DSP stage in the float pipeline (or via libvlc's built-in EQ). Preamp = a single gain stage before/after the band filters; include headroom/clip handling as mpv's --replaygain-clip does.

=====================================================================
4. PCM TAP FOR VISUALIZATIONS (AVS / MILKDROP / projectM)
=====================================================================
- libvlc: libvlc_audio_set_callbacks + set_format -> decoded PCM buffers with play/pause/resume/flush/drain callbacks. Cleanest app-side tap of the mainstream engines. https://raw.githubusercontent.com/videolan/vlc-3.0/master/include/vlc/libvlc_media_player.h
- GStreamer: tee + appsink (raw audio/x-raw F32LE) or the spectrum element for FFT. https://gstreamer.freedesktop.org/documentation/app/appsink.html , https://gstreamer.freedesktop.org/documentation/coreelements/tee.html
- BASS: BASS_ChannelGetData(..., BASS_DATA_FLOAT | BASS_DATA_FFT...) -> PCM/FFT directly. https://github.com/ManagedBass/ManagedBass
- mpv: NO callback (PR #5566 rejected). Only --ao=pcm to file/FIFO or in-mpv lavfi rendering. This is the key architectural weakness for a MilkDrop-first app. https://github.com/mpv-player/mpv/pull/5566
- projectM (MilkDrop): cross-platform MilkDrop-compatible lib, LGPL-2.1, latest v4.1.8. Presets are pixel-shader + Milkdrop equations; the lib needs float PCM via projectm_pcm_add_float and an OpenGL context. No official .NET binding -> P/Invoke + native shipping; for Avalonia (Skia), render to an FBO/native surface and composite, or embed an OpenGL control.
  - https://github.com/projectM-visualizer/projectm
  - https://github.com/projectM-visualizer/projectm/releases (v4.1.8)
  - License: https://raw.githubusercontent.com/projectM-visualizer/projectm/master/LICENSE.txt (LGPL-2.1)
- AVS (Advanced Visualization Studio): modern C++ port vis_avs (BSD-3-Clause): https://github.com/grandchild/vis_avs ; Rust port avs-rs: https://github.com/csabex/vis_avs ; original sources: https://github.com/visbot/vis_avs . No .NET binding.
- Built-in FFmpeg-based visualizers (showcqt, showspectrum, avectorscope) are an alternative for mpv via --lavfi-complex but render inside the engine, not into app UI: https://blog.fraggod.net/2018/04/12/mpv-audio-visualization.html

=====================================================================
5. CROSS-PLATFORM AUDIO OUTPUT APIS IN 2026
=====================================================================
- Windows: WASAPI (shared + exclusive). Exclusive mode = bit-perfect/direct device access; the app must match the device format. mpv exposes it as --audio-exclusive=yes with --wasapi-exclusive-buffer tuning; NAudio offers WasapiOut exclusive mode; Microsoft's docs:
  - https://raw.githubusercontent.com/mpv-player/mpv/master/DOCS/man/ao.rst (wasapi section)
  - https://raw.githubusercontent.com/mpv-player/mpv/master/DOCS/man/options.rst (--audio-exclusive)
  - https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams
  - https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-formats
  - CamillaDSP WASAPI notes: https://github.com/HEnquist/camilladsp/blob/master/backend_wasapi.md
- macOS: CoreAudio via AudioUnits (mpv 'coreaudio') and coreaudio_exclusive (direct device access, bypasses the sound server, i.e. 'hog mode'). Hardware sample-rate switching is opt-in via --coreaudio-change-physical-format=yes.
  - https://raw.githubusercontent.com/mpv-player/mpv/master/DOCS/man/ao.rst (coreaudio / coreaudio_exclusive)
  - CoreAudio hog mode discussion: https://developer.apple.com/forums/thread/70274
  - mpv coreaudio_exclusive source: https://github.com/mpv-player/mpv/blob/master/audio/out/ao_coreaudio_exclusive.c
- Linux: PipeWire is the default audio server on Fedora, Ubuntu (22.10+, incl. 24.04/26.04), and Arch; pipewire-pulse provides PulseAudio compatibility, version line 1.6.x in 2026. mpv has a native pipewire AO (--pipewire-buffer, --pipewire-remote, --pipewire-volume-mode); VLC/GStreamer route via pipewire/pulse/alsa sinks.
  - https://docs.pipewire.org/page_man_pipewire-pulse_1.html
  - https://man.archlinux.org/man/pipewire-pulse.1.en
  - https://manpages.ubuntu.com/manpages/noble/man1/pipewire-pulse.1.html
  - https://archlinux.org/packages/extra/x86_64/pipewire-pulse (1:1.6.9)
  - https://raw.githubusercontent.com/mpv-player/mpv/master/DOCS/man/ao.rst (pipewire section)
- How the engines route: mpv auto-probes wasapi/coreaudio/pipewire/pulse/alsa (--ao=help); libvlc uses its aout modules; GStreamer uses wasapisink/wasapi2sink, osxaudiosink, pipewiresink, pulsesink, alsasink with autoplug by playbin. The app can force selection per platform.

=====================================================================
6. DSP CHAIN IN .NET (EQ + ReplayGain + TEMPO) WITHOUT GLITCHING
=====================================================================
Design rule: keep the whole chain in one sample format (float32, interleaved or planar) at one sample rate, process in fixed-size blocks on a dedicated high-priority/real-time thread, and use a lock-free ring buffer between decode/producer and output/consumer. Avoid per-sample allocation (Span<float>/ArrayPool). Never allocate in the audio callback.

- EQ (biquad): 10 peaking biquads in series (Direct Form I or Transposed DF-II), coefficients from the RBJ cookbook. In .NET: NAudio BiQuadFilter (PeakingEQ/LowShelf/HighShelf) or NWaves. Recompute coefficients only when a gains/preamp value changes; smooth gain changes (ramp) to avoid zipper noise.
  - https://github.com/naudio/NAudio
  - https://github.com/ar1st0crat/NWaves
- ReplayGain: read RG/R128 tags (see metadata section), then apply gain = track-or-album gain + preamp, with a peak-aware clamp to avoid clipping (mirror mpv's --replaygain-clip). If you use mpv, this is native. If you apply it yourself, it is just a scalar gain stage in the float pipeline.
- Tempo/pitch: SoundTouch.Net 2.3.2 is a complete C# rewrite of SoundTouch (MIT) with an NAudio adapter, allowing independent tempo/pitch/rate; ranges roughly -95%..+... : 
  - https://www.nuget.org/packages/SoundTouch.Net
  - https://www.nuget.org/packages/SoundTouch.Net.NAudioSupport
  - http://soundtouch.surina.net/README.html
  Native alternatives (if you P/Invoke): SoundTouch, RubberBand (mpv's 'rubberband' filter, https://breakfastquay.com/rubberband/), or FFmpeg atempo/scaletempo2 as used by mpv.
- High-quality resampling (needed for sample-rate conversion / device matching): libsoxr (https://github.com/chirlu/soxr), libsamplerate (https://libsndfile.github.io/libsamplerate/), zita-resampler (https://kokkinizita.linuxaudio.org/linuxaudio/zita-resampler/resampler.html , https://github.com/digital-stage/zita-resampler). NAudio ships resamplers (WDL, Media Foundation on Windows). Avoid resampling entirely on the bit-perfect path.
- Bit-perfect passthrough: requires (a) no EQ/RG/volume/tempo/resample in the chain, (b) exclusive output (WASAPI exclusive / CoreAudio exclusive / PipeWire bit-perfect), and (c) an integer path that does not convert to float. Any DSP stage, system mixer, or sample-rate conversion breaks bit-perfect. So expose a 'bit-perfect mode' that disables the entire DSP chain and forces exclusive device format. References: WASAPI exclusive docs (above); WASAPI exclusive guide: https://aurisplayer.com/blog/wasapi-exclusive-guide.html
- Placeholder caution: there is no credible 'RWKV' audio DSP for this; ignore that suggestion - use biquads/SoundTouch/SoundTouch.Net.

=====================================================================
7. METADATA / TAGS + LIBRARIES
=====================================================================
- TagLib# (mono/taglib-sharp): LGPL-2.1, latest TagLibSharp 2.3.0 (2022-08), .NET Standard 2.0 / .NET Framework 4.6.2. Battle-tested, reads/writes ID3v1/v2, APE, Vorbis, MP4/iTunes, FLAC, etc. Maintenance is slow but it is the safe default.
  - https://github.com/mono/taglib-sharp
  - https://github.com/mono/taglib-sharp/releases
  - https://www.nuget.org/packages/TagLibSharp/2.3.0
- TagLibSharp2 (decriptor/TagLibSharp2): MIT, clean-room, .NET 8/10 + netstandard2.0/2.1, nullable/Span/async, result-based errors. Wide format support incl. APE, WavPack, Musepack, DSF/DFF (DSD), ASF/WMA, MP4/M4A (AAC/ALAC), Ogg FLAC/Opus, with ReplayGain, R128 gain, MusicBrainz IDs, album art. Latest 0.6.0 (2026-01-05); young (32 stars) - watch it, but do not bet the project on it yet.
  - https://github.com/decriptor/taglibsharp2
  - https://raw.githubusercontent.com/decriptor/taglibsharp2/main/CHANGELOG.md
- ATL.NET (z440.atl.core) 7.10.0: strong APE/Musepack/tracker tag handling (originally Audio Tools Library). Useful when TagLib# mishandles obscure tags.
  - https://github.com/Zeugma440/atldotnet
  - https://github.com/Zeugma440/atldotnet/wiki/3.-Usage-_-Code-snippets
  - https://libraries.io/nuget/z440.atl.core
- 'Metadata Kernel': no notable .NET audio-tag library by that name was found; treat as [UNVERIFIED] / not a viable dependency. (MetadataExtractor exists but targets image/video metadata, not audio tags.)
- Album art + folder conventions: embedded art (ID3 APIC, Vorbis METADATA_BLOCK_PICTURE, MP4 covr, APE) is exposed via the tag libs (TagLib# IPicture / TagLibSharp2 IPicture). Folder conventions: folder.jpg (Windows/WMP), cover.jpg/album.jpg/front.jpg (Linux/foobar common). Implement a lookup order: embedded -> folder.jpg -> cover.jpg -> *front*.jpg.
- MusicBrainz / dedupe:
  - Chromaprint is the fingerprint library (MIT/LGPL mix - as a whole LGPL-2.1): https://acoustid.org/chromaprint , https://github.com/acoustid/chromaprint
  - AcoustID is the lookup service: https://musicbrainz.org/doc/AcoustID , https://musicbrainz.org/doc/Fingerprinting
  - Pure .NET fingerprinting + webservice client: AcoustID.NET (LGPL-2.1 netstandard2.0): https://github.com/wo80/AcoustID.NET
  - Native binding alternative: libchromaprint + P/Invoke.
  - MusicBrainz API client (MIT): MetaBrainz.MusicBrainz: https://github.com/Zastai/MetaBrainz.MusicBrainz
- CUE / AccurateRip: CUETools.NET (GPL) contains CUESheet + AccurateRip + CTDB: https://github.com/gchudov/cuetools.net (CUESheet.cs, CUETools.AccurateRip). GPL is viral - do not link into a proprietary app; use it as a reference or a separate process. For a permissive parser, libcue (BSD).

=====================================================================
8. LOCAL LIBRARY SEARCH INDEX (100k TRACKS)
=====================================================================
- SQLite FTS5: built into SQLite; provides full-text search with BM25 ranking and porter/trigram/unicode61 tokenizers: https://sqlite.org/fts5.html . Microsoft.Data.Sqlite bundles SQLitePCLRaw (e_sqlite3), which ships FTS5 enabled; you can also use SQLitePCLRaw.bundle_e_sqlite3 directly. [FTS5-in-bundle: strongly supported by SQLitePCLRaw's default build; verify at runtime with 'PRAGMA compile_options'. URL: https://learn.microsoft.com/en-us/dotnet/standard/data/sqlite/custom-versions]
  - Independent FTS5-enabled ADO.NET bundle example: https://github.com/rsarov/SQLiteFTS5NET
- Lucene.NET: Apache-2.0, but still pre-1.0 - latest published is 4.8.0-beta00018 (download page), and the Aug 2026 status update shows beta19 still in progress, porting the Java 4.8 codebase: https://lucenenet.apache.org/download/version-4.8.0-beta00018.html , https://github.com/apache/lucenenet/discussions/1433 (Aug 2026), https://github.com/apache/lucenenet/discussions/1401 (Jul 2026). No stable release since the 2018 betas; heavy on memory/disk and deployment.
- BLEVE is Go, not .NET; not applicable.
- Recommendation: for a local 100k-track library, SQLite FTS5 is more than sufficient (FTS5 comfortably handles millions of rows). Keep
  track/album/artist/albumartist as a normalised table plus an FTS5 virtual table (title/artist/album/albumartist/composer) with the porter or trigram tokenizer, and use BM25 with column weights. Add prefix/typo tolerance via trigram tokenizer or a secondary 'LIKE' filter. Only escalate to Lucene.NET if you need very large fuzzy/faceted retrieval with custom analyzers and can tolerate a beta dependency.

=====================================================================
9. SUGGESTED ENGINE ARCHITECTURE (for the feasibility/spec phase)
=====================================================================
- Engine facade: IAudioplayer with libvlc primary; libmpv optional secondary (LGPL build). Encapsulate engine choice so output/EQ/tap capabilities are queried per backend.
- Decoding: FFmpeg (inside libvlc) + libopenmpt P/Invoke for trackers. CUE via engine/CUETools-as-reference.
- EQ: use libvlc's native Winamp EQ when on libvlc; otherwise a .NET 10-band peaking biquad chain (NAudio/NWaves) with preamp and clip guard.
- Visualization: consume PCM from libvlc audio callbacks (or GStreamer appsink) -> projectM (MilkDrop, LGPL) and vis_avs/avs-rs (AVS) via P/Invoke, rendering OpenGL into an Avalonia-composited surface. Avoid mpv-only designs, which cannot tap PCM.
- Output: per-platform choice WASAPI (exclusive option) / CoreAudio (exclusive/hog, optional hardware SR switch) / PipeWire, selecting the engine's AO where possible.
- DSP: float32 block pipeline, SoundTouch.Net for tempo, ReplayGain apply with clip guard, optional bit-perfect bypass that disables all DSP and forces exclusive output.
- Metadata: TagLib# default, TagLibSharp2/ATL.NET fallbacks; AcoustID.NET + MetaBrainz.MusicBrainz for fingerprint/dedupe.
- Library: SQLite (track/album/artist/playlist tables) + FTS5 search; folder.jpg/cover.jpg art resolver.
- Skins/visualizers are out of scope for this ticket but the PCM tap requirement constrains engine choice (favours libvlc/GStreamer/BASS over mpv).

=====================================================================
10. KEY RISKS / OPEN QUESTIONS
=====================================================================
1. mpv cannot feed a custom visualizer with PCM; if MilkDrop/AVS is a hard requirement, mpv is a poor primary engine unless you accept --ao=pcm FIFO hacks or decode separately. (https://github.com/mpv-player/mpv/pull/5566)
2. libvlc maxes at the 3.0.x API; VLC 4.0 timing is uncertain, and LibVLCSharp 4.x is preview-only (feedz.io). https://github.com/videolan/libvlcsharp/blob/3.x/README.md
3. LGPL compliance for libmpv/GStreamer/FFmpeg: dynamic linking + no GPL components; ship license texts and allow relinking. mpv LGPL build disables some features (X11 VO, jack, DVD/CDDA). https://raw.githubusercontent.com/mpv-player/mpv/master/Copyright
4. libvlc EQ headroom: Winamp EQ boosts can clip; libvlc's equalizer does not auto-attenuate. Implement preamp/clip handling. [INFERENCE]
5. projectM/AVS have no .NET bindings; budget for native packaging + OpenGL/Avalonia interop. https://github.com/projectM-visualizer/projectm
6. TagLib# maintenance is slow; have a migration plan to TagLibSharp2 or ATL.NET for edge formats. https://github.com/mono/taglib-sharp/releases
7. Lucene.NET is not a safe dependency (perpetual beta); default to FTS5. https://github.com/apache/lucenenet/discussions/1433

=====================================================================
REFERENCE URLS (consolidated)
=====================================================================
mpv: https://github.com/mpv-player/mpv/releases/tag/v0.41.0 | https://raw.githubusercontent.com/mpv-player/mpv/master/Copyright | https://raw.githubusercontent.com/mpv-player/mpv/master/DOCS/man/ao.rst | https://raw.githubusercontent.com/mpv-player/mpv/master/DOCS/man/af.rst | https://raw.githubusercontent.com/mpv-player/mpv/master/DOCS/man/options.rst | https://github.com/mpv-player/mpv/pull/5566 | https://github.com/mpv-player/mpv/issues/9283 | https://github.com/mpv-player/mpv/issues/2033
VLC/LibVLCSharp: https://videolan.videolan.me/vlc-3.0/group__libvlc__audio.html | https://raw.githubusercontent.com/videolan/vlc-3.0/master/include/vlc/libvlc_media_player.h | https://raw.githubusercontent.com/videolan/vlc/master/modules/audio_filter/equalizer_presets.h | https://github.com/videolan/vlc/blob/master/modules/audio_filter/equalizer.c | https://github.com/videolan/libvlcsharp/blob/3.x/NEWS | https://github.com/videolan/libvlcsharp/blob/3.x/README.md | https://www.videolan.org/vlc/releases/3.0.21.html
GStreamer: https://gstreamer.freedesktop.org/releases/1.28/ | https://gstreamer.freedesktop.org/documentation/plugin-development/appendix/licensing-advisory.html | https://gstreamer.freedesktop.org/documentation/installing/on-windows.html | https://gstreamer.freedesktop.org/documentation/deploying/mac-osx.html | https://gstreamer.freedesktop.org/documentation/equalizer/equalizer-10bands.html | https://gstreamer.freedesktop.org/documentation/coreelements/tee.html | https://gstreamer.freedesktop.org/documentation/app/appsink.html | https://github.com/gircore/gir.core/releases
FFmpeg: https://ffmpeg.org/doxygen/trunk/md_LICENSE.html | https://ffmpeg.org/ffmpeg-codecs.html | https://ffmpeg.org/pipermail/ffmpeg-devel/2025-August/347886.html | https://github.com/BtbN/FFmpeg-Builds
BASS: http://www.un4seen.com/bass.html | https://www.un4seen.com/license.php?l=1 | https://www.radio42.com/bass/bass_purchase.html | https://github.com/ManagedBass/ManagedBass
NAudio/CSCore: https://raw.githubusercontent.com/naudio/NAudio/main/RELEASE_NOTES.md | https://github.com/naudio/NAudio | https://github.com/filoe/cscore
Tap/visualizers: https://github.com/projectM-visualizer/projectm | https://github.com/grandchild/vis_avs | https://github.com/csabex/vis_avs | https://github.com/visbot/vis_avs | https://github.com/ar1st0crat/NWaves
Output APIs: https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams | https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-formats | https://github.com/HEnquist/camilladsp/blob/master/backend_wasapi.md | https://docs.pipewire.org/page_man_pipewire-pulse_1.html | https://man.archlinux.org/man/pipewire-pulse.1.en | https://manpages.ubuntu.com/manpages/noble/man1/pipewire-pulse.1.html | https://developer.apple.com/forums/thread/70274 | https://github.com/mpv-player/mpv/blob/master/audio/out/ao_coreaudio_exclusive.c
DSP: https://www.nuget.org/packages/SoundTouch.Net | https://www.nuget.org/packages/SoundTouch.Net.NAudioSupport | http://soundtouch.surina.net/README.html | https://github.com/chirlu/soxr | https://libsndfile.github.io/libsamplerate/ | https://kokkinizita.linuxaudio.org/linuxaudio/zita-resampler/resampler.html | https://wiki.hydrogenaudio.org/index.php?title=ReplayGain_2.0_specification
Metadata/fingerprint: https://github.com/mono/taglib-sharp | https://github.com/decriptor/taglibsharp2 | https://raw.githubusercontent.com/decriptor/taglibsharp2/main/CHANGELOG.md | https://github.com/Zeugma440/atldotnet | https://acoustid.org/chromaprint | https://github.com/acoustid/chromaprint | https://musicbrainz.org/doc/AcoustID | https://github.com/wo80/AcoustID.NET | https://github.com/Zastai/MetaBrainz.MusicBrainz | https://github.com/gchudov/cuetools.net
Search: https://sqlite.org/fts5.html | https://learn.microsoft.com/en-us/dotnet/standard/data/sqlite/custom-versions | https://lucenenet.apache.org/download/version-4.8.0-beta00018.html | https://github.com/apache/lucenenet/discussions/1433
libopenmpt: https://lib.openmpt.org/libopenmpt/2025/05/31/release-0.8.0 | https://lib.openmpt.org/libopenmpt/license/ | https://github.com/OpenMPT/openmpt