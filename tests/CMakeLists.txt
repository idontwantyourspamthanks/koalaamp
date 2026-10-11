# SPDX-License-Identifier: MIT
# Tests for the FE-0 skeleton: prove the harness builds, links and runs on all
# three platforms before anything depends on it.

include(FetchContent)

# Test framework: Catch2 v3, pinned by URL + SHA256. It is fetched into the
# build tree (not vendored into third_party/, which stays empty this ticket);
# the choice and the alternative are recorded in docs/spikes/fe0-deps.md.
FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
    URL_HASH SHA256=c991b247a1a0d7bb9c39aa35faf0fe9e19764213f28ffba3109388e62ee0269c
    DOWNLOAD_EXTRACT_TIMESTAMP OFF)
FetchContent_MakeAvailable(Catch2)

add_executable(koalaamp_tests skeleton_test.cpp)
koalaamp_enable_warnings(koalaamp_tests)
target_include_directories(koalaamp_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(koalaamp_tests PRIVATE Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(koalaamp_tests)

# FE-5: koalapaw Jellyfin port — unit tests + offline fixture replay through the
# same JellyfinClient code path the smoke binary uses live.
add_executable(jellyfin_port_tests jellyfin_port_test.cpp)
koalaamp_enable_warnings(jellyfin_port_tests)
target_include_directories(jellyfin_port_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/third_party/include")
target_link_libraries(jellyfin_port_tests PRIVATE Catch2::Catch2WithMain koalaamp_jellyfin)
target_compile_definitions(jellyfin_port_tests PRIVATE
    KOALAAMP_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/jellyfin"
    KOALAAMP_FIXTURE_UNAUTHORIZED_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/jellyfin-unauthorized"
    KOALAAMP_SERVERS_SRC="${CMAKE_SOURCE_DIR}/src/servers")
catch_discover_tests(jellyfin_port_tests)

# Headless smoke test: launch the real application, prove the frameless toplevel
# constructs and maps, then exit 0. Works on every desktop platform via Qt's
# offscreen platform plugin.
add_test(NAME app_smoke COMMAND $<TARGET_FILE:koalaamp> --self-test)
set_tests_properties(app_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30)

# FE-2: guard for the spec §3.1 EQ rule (native EQ runs upstream of the libvlc
# audio callback, so DSP mode never enables it). Built only where libvlc is
# found; on machines without libvlc the test simply does not exist rather than
# failing — but CI installs libvlc-dev on Linux so the rule is enforced there.
if(KOALAAMP_HAS_LIBVLC)
    add_executable(koalaamp_vlc_tests libvlc_eq_test.cpp)
    koalaamp_enable_warnings(koalaamp_vlc_tests)
    target_link_libraries(koalaamp_vlc_tests PRIVATE Catch2::Catch2WithMain koalaamp_vlc)
    catch_discover_tests(koalaamp_vlc_tests)
    message(STATUS "KoalaAmp: libvlc EQ-rule test enabled")
endif()

# FE-4: PlaybackCore end-to-end (transport/seek/volume/EndReached/queue), the
# core-level EQ rule + Qt-free static guard, and the 200-track-change soak.
# Gated on the core target because it wraps the libvlc chain; the soak gets a
# generous timeout (200 real track activations, slower under ASAN).
if(TARGET koalaamp_playback_core)
    add_executable(koalaamp_playback_tests playback_core_test.cpp)
    koalaamp_enable_warnings(koalaamp_playback_tests)
    target_link_libraries(koalaamp_playback_tests PRIVATE Catch2::Catch2WithMain
        koalaamp_playback_core)
    target_compile_definitions(koalaamp_playback_tests PRIVATE
        "KOALAAMP_PLAYBACK_SRC=\"${CMAKE_SOURCE_DIR}/src/playback\"")
    catch_discover_tests(koalaamp_playback_tests PROPERTIES TIMEOUT 900)
    message(STATUS "KoalaAmp: FE-4 PlaybackCore tests enabled")
endif()

# FE-3: WSZ/BMP decode + ported sprite table. Decoder cases use synthetic BMPs;
# the base-2.91 cases skip when the gitignored fixture has not been fetched
# (spikes/fe3_wsz/fetch-skin.sh).
if(TARGET koalaamp_fe3_wsz)
    add_executable(koalaamp_fe3_tests fe3_wsz_test.cpp)
    koalaamp_enable_warnings(koalaamp_fe3_tests)
    target_link_libraries(koalaamp_fe3_tests PRIVATE Catch2::Catch2WithMain koalaamp_fe3_wsz)
    target_compile_definitions(koalaamp_fe3_tests PRIVATE
        "KOALAAMP_FE3_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\"")
    # Windows note: the test exe links Qt + zlib DLLs; Catch2's build-time
    # discovery needs them on PATH. That is provided once, at the CI level
    # (vcpkg installed/x64-windows/bin prepended to PATH, and Qt's bin via
    # the toolchain) — deliberately NOT per-test here: $<TARGET_RUNTIME_DLL_DIRS>
    # only covers SHARED targets and FindZLIB's imported target is UNKNOWN,
    # so a per-test override is redundant at best and can clobber the working
    # PATH if the genex fails to expand in ENVIRONMENT.
    catch_discover_tests(koalaamp_fe3_tests)
    message(STATUS "KoalaAmp: FE-3 WSZ decode/table tests enabled")
endif()

# SE-1: WSZ loader unit tests (all fixtures synthetic), the 30-skin corpus
# harness (SKIPs when the gitignored corpus has not been fetched:
# scripts/fetch-corpus.sh) and the fuzz target (100 mutated containers — the
# target the ASAN/UBSAN parser build runs).
add_executable(koalaamp_skins_tests skins_test.cpp)
koalaamp_enable_warnings(koalaamp_skins_tests)
target_include_directories(koalaamp_skins_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(koalaamp_skins_tests PRIVATE Catch2::Catch2WithMain koalaamp_skins)
catch_discover_tests(koalaamp_skins_tests)

add_executable(koalaamp_skins_corpus_tests skins_corpus_test.cpp)
koalaamp_enable_warnings(koalaamp_skins_corpus_tests)
target_link_libraries(koalaamp_skins_corpus_tests PRIVATE Catch2::Catch2WithMain koalaamp_skins)
target_compile_definitions(koalaamp_skins_corpus_tests PRIVATE
    "KOALAAMP_CORPUS_DIR=\"${CMAKE_SOURCE_DIR}/third_party/skins-corpus\"")
catch_discover_tests(koalaamp_skins_corpus_tests)

add_executable(koalaamp_skins_fuzz_tests skins_fuzz_test.cpp)
koalaamp_enable_warnings(koalaamp_skins_fuzz_tests)
target_include_directories(koalaamp_skins_fuzz_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(koalaamp_skins_fuzz_tests PRIVATE Catch2::Catch2WithMain koalaamp_skins)
target_compile_definitions(koalaamp_skins_fuzz_tests PRIVATE
    "KOALAAMP_FUZZ_SEED=\"${CMAKE_SOURCE_DIR}/third_party/skins-corpus/5e4f10275dcb1fb211d4a8b4f1bda236.wsz\"")
catch_discover_tests(koalaamp_skins_fuzz_tests)
message(STATUS "KoalaAmp: SE-1 WSZ loader tests enabled (unit + corpus + fuzz)")

# AU-1: the DSP pipeline (ReplayGain, 10-band EQ, preamp, limiter), the
# .eqf/winamp.q1 codec, golden transfer functions, the latency and
# allocation-free soak, and the no-native-EQ guard generalised to src/audio.
# The seam test (PlaybackCore::setPcmCallback) is compiled in when the core is
# available, which is exactly where the transform gets installed.
add_executable(koalaamp_dsp_tests dsp_test.cpp)
koalaamp_enable_warnings(koalaamp_dsp_tests)
target_link_libraries(koalaamp_dsp_tests PRIVATE Catch2::Catch2WithMain koalaamp_dsp)
target_compile_definitions(koalaamp_dsp_tests PRIVATE
    "KOALAAMP_AUDIO_SRC=\"${CMAKE_SOURCE_DIR}/src/audio\""
    "KOALAAMP_EQ_FIXTURE_DIR=\"${CMAKE_SOURCE_DIR}/tests/fixtures/eq\"")
if(TARGET koalaamp_playback_core)
    target_link_libraries(koalaamp_dsp_tests PRIVATE koalaamp_playback_core)
    target_compile_definitions(koalaamp_dsp_tests PRIVATE KOALAAMP_HAS_PLAYBACK_CORE)
endif()
catch_discover_tests(koalaamp_dsp_tests PROPERTIES TIMEOUT 600)
message(STATUS "KoalaAmp: AU-1 DSP tests enabled")

# SE-2: the classic main window. The renderer/formula/golden suite and the
# 30-skin render smoke need only Qt Gui; the interaction-parity suite drives the
# real widget and therefore Qt Widgets + QtTest (mouse/key/wheel synthesis).
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(koalaamp_se2_render_tests se2_render_test.cpp)
koalaamp_enable_warnings(koalaamp_se2_render_tests)
target_link_libraries(koalaamp_se2_render_tests PRIVATE Catch2::Catch2WithMain koalaamp_ui)
target_compile_definitions(koalaamp_se2_render_tests PRIVATE
    "KOALAAMP_SE2_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\""
    "KOALAAMP_SE2_GOLDEN_MANIFEST=\"${CMAKE_SOURCE_DIR}/tests/fixtures/se2/goldens.sha256\"")
catch_discover_tests(koalaamp_se2_render_tests)

add_executable(koalaamp_se2_interaction_tests se2_interaction_test.cpp)
koalaamp_enable_warnings(koalaamp_se2_interaction_tests)
target_link_libraries(koalaamp_se2_interaction_tests PRIVATE Catch2::Catch2 koalaamp_ui Qt6::Test)
target_compile_definitions(koalaamp_se2_interaction_tests PRIVATE
    "KOALAAMP_SE2_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\""
    "KOALAAMP_CORPUS_DIR=\"${CMAKE_SOURCE_DIR}/third_party/skins-corpus\"")
# Widget tests need a platform plugin; the offscreen one exists everywhere.
# DISCOVERY_MODE PRE_TEST: Catch2's default build-time discovery executes the
# exe directly where no test ENVIRONMENT applies; PRE_TEST defers discovery to
# ctest time. Belt-and-braces: CI also exports QT_QPA_PLATFORM=offscreen at
# the job level (build tool + ctest inherit it), so neither the discovery
# invocation nor any run can pick xcb on a headless runner.
#
# No separate hand-declared smoke add_test: the discovered "SE-2 interaction
# parity checklist" entry IS the suite (a raw add_test would need its own
# SKIP_RETURN_CODE wiring to treat Catch2's skip exit 4 as pass — the
# discovered tests already handle that; a duplicate wrapper just misreports
# skips as failures).
catch_discover_tests(koalaamp_se2_interaction_tests PROPERTIES
    DISCOVERY_MODE PRE_TEST
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120)

add_executable(koalaamp_se2_corpus_tests se2_corpus_render_test.cpp)
koalaamp_enable_warnings(koalaamp_se2_corpus_tests)
target_link_libraries(koalaamp_se2_corpus_tests PRIVATE Catch2::Catch2WithMain koalaamp_ui)
target_compile_definitions(koalaamp_se2_corpus_tests PRIVATE
    "KOALAAMP_CORPUS_DIR=\"${CMAKE_SOURCE_DIR}/third_party/skins-corpus\"")
catch_discover_tests(koalaamp_se2_corpus_tests PROPERTIES TIMEOUT 300)
message(STATUS "KoalaAmp: SE-2 main-window tests enabled (render/goldens, interaction, corpus)")
