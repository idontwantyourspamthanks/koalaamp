# SPDX-License-Identifier: MIT
# audio — AU-1 DSP pipeline (ReplayGain, 10-band EQ, preamp, limiter) and the
# .eqf/winamp.q1 preset codec. Qt-FREE and libvlc-FREE by contract: it depends
# only on the playback interfaces (docs/spec.md §2.1-2.3). The transform is
# installed at the engine seam (`IAudioEngine::setPcmCallback`) by its consumers.
add_library(koalaamp_dsp STATIC
    AlbumEqStore.cpp
    AlbumEqStore.hpp
    Biquad.hpp
    DspPipeline.cpp
    DspPipeline.hpp
    Equalizer.cpp
    Equalizer.hpp
    EqPreset.cpp
    EqPreset.hpp
    EqValue.hpp
    Limiter.hpp
    ReplayGain.cpp
    ReplayGain.hpp
    RgTags.cpp
    RgTags.hpp)
koalaamp_enable_warnings(koalaamp_dsp)
target_include_directories(koalaamp_dsp PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(koalaamp_dsp PUBLIC koalaamp_playback)
