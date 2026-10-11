# SPDX-License-Identifier: MIT
# FE-2 spike: the libvlc decoded-PCM callback path (raw C API) + a miniaudio
# sink, plus the measurement harness. Built only when libvlc and the vendored
# miniaudio header are both present (src/interop/CMakeLists.txt gates it).
#
# Warnings are not enabled on this library: the miniaudio implementation TU is
# third-party (MIT-0) and is not ours to make -Wall clean. Our own headers are
# warning-clean by construction and the consumers (spike exe, tests) do enable
# warnings.

if(TARGET PkgConfig::LIBVLC)
    set(KOALAAMP_VLC_TARGET PkgConfig::LIBVLC)
else()
    set(KOALAAMP_VLC_TARGET LibVLC::LibVLC)
endif()

add_library(koalaamp_vlc STATIC VlcPcmTap.cpp MiniAudioSink.cpp)
target_include_directories(koalaamp_vlc PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/third_party/include")
target_link_libraries(koalaamp_vlc PUBLIC ${KOALAAMP_VLC_TARGET})

if(UNIX AND NOT APPLE)
    # miniaudio dlopens libasound/libpulse at runtime; it still needs dl/pthread/m.
    target_link_libraries(koalaamp_vlc PUBLIC ${CMAKE_DL_LIBS} pthread m)
endif()

add_executable(koalaamp_vlc_spike vlc_spike.cpp)
koalaamp_enable_warnings(koalaamp_vlc_spike)
target_link_libraries(koalaamp_vlc_spike PRIVATE koalaamp_vlc)
