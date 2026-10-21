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

# Catch2's default POST_BUILD discovery executes every test exe while building.
# On Windows that hides a missing runtime DLL behind an MSBuild failure, so defer
# discovery to ctest, where the job's full PATH is available.
if(WIN32)
    set(CMAKE_CATCH_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)
endif()

add_executable(koalaamp_tests skeleton_test.cpp)
koalaamp_enable_warnings(koalaamp_tests)
target_include_directories(koalaamp_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(koalaamp_tests PRIVATE Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(koalaamp_tests)

add_executable(single_instance_tests single_instance_test.cpp)
koalaamp_enable_warnings(single_instance_tests)
target_link_libraries(single_instance_tests PRIVATE Catch2::Catch2WithMain koalaamp_platform)
catch_discover_tests(single_instance_tests)

add_executable(migration_fuzz_tests migration_fuzz_test.cpp)
koalaamp_enable_warnings(migration_fuzz_tests)
target_include_directories(migration_fuzz_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(migration_fuzz_tests PRIVATE Catch2::Catch2WithMain koalaamp_migration)
catch_discover_tests(migration_fuzz_tests)

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
    KOALAAMP_FIXTURE_AUTH_REJECTED_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/jellyfin-auth-rejected"
    KOALAAMP_SERVERS_SRC="${CMAKE_SOURCE_DIR}/src/servers")
catch_discover_tests(jellyfin_port_tests)

# QuickConnect's Initiate is a POST with no body. libcurl reads that body from
# stdin unless the transport sets a zero length, and the transfer timeout does
# not interrupt the read — the dialog stays on "QuickConnect…" for as long as
# the terminal is open. Loopback only; the failure path exits rather than hangs.
add_executable(http_transport_tests http_transport_test.cpp)
koalaamp_enable_warnings(http_transport_tests)
target_include_directories(http_transport_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(http_transport_tests PRIVATE Catch2::Catch2WithMain koalaamp_jellyfin)
catch_discover_tests(http_transport_tests PROPERTIES TIMEOUT 20)

# SV-2: direct-vs-transcode planning + /Sessions/Playing* player wiring.
add_executable(jellyfin_playback_tests jellyfin_playback_test.cpp)
koalaamp_enable_warnings(jellyfin_playback_tests)
target_include_directories(jellyfin_playback_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/third_party/include")
target_link_libraries(jellyfin_playback_tests PRIVATE
    Catch2::Catch2WithMain koalaamp_jellyfin)
target_compile_definitions(jellyfin_playback_tests PRIVATE
    KOALAAMP_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/jellyfin")
catch_discover_tests(jellyfin_playback_tests)

# SV-2: /socket remote control (scripted transport, no network).
add_executable(jellyfin_remote_tests jellyfin_remote_test.cpp)
koalaamp_enable_warnings(jellyfin_remote_tests)
target_include_directories(jellyfin_remote_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(jellyfin_remote_tests PRIVATE
    Catch2::Catch2WithMain koalaamp_jellyfin)
catch_discover_tests(jellyfin_remote_tests)

# SV-5: server browse gateway (libraries/albums/playlists/rating/source).
add_executable(jellyfin_browse_tests jellyfin_browse_test.cpp)
koalaamp_enable_warnings(jellyfin_browse_tests)
target_include_directories(jellyfin_browse_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(jellyfin_browse_tests PRIVATE
    Catch2::Catch2WithMain koalaamp_jellyfin)
target_compile_definitions(jellyfin_browse_tests PRIVATE
    KOALAAMP_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/jellyfin")
catch_discover_tests(jellyfin_browse_tests)

# LI-1/LI-2: scanner, incremental stamps, watcher, SQLite/FTS5.
add_executable(library_tests library_test.cpp)
koalaamp_enable_warnings(library_tests)
target_include_directories(library_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(library_tests PRIVATE Catch2::Catch2WithMain koalaamp_library)
target_compile_definitions(library_tests PRIVATE
    KOALAAMP_LIBRARY_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/library")
catch_discover_tests(library_tests)

# SV-3: Plex auth/browse/source fixtures.
add_executable(plex_port_tests plex_port_test.cpp)
koalaamp_enable_warnings(plex_port_tests)
target_include_directories(plex_port_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/third_party/include")
target_link_libraries(plex_port_tests PRIVATE
    Catch2::Catch2WithMain koalaamp_plex koalaamp_jellyfin)
target_compile_definitions(plex_port_tests PRIVATE
    KOALAAMP_PLEX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/plex")
catch_discover_tests(plex_port_tests)

# SV-4: Plex timeline/watched/profile/media source.
add_executable(plex_playback_tests plex_playback_test.cpp)
koalaamp_enable_warnings(plex_playback_tests)
target_include_directories(plex_playback_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(plex_playback_tests PRIVATE
    Catch2::Catch2WithMain koalaamp_plex koalaamp_jellyfin)
target_compile_definitions(plex_playback_tests PRIVATE
    KOALAAMP_PLEX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/plex")
catch_discover_tests(plex_playback_tests)

# SV-4: libvlc plays a PMS part through the callback media source.
if(KOALAAMP_HAS_LIBVLC)
    add_executable(plex_stream_libvlc_tests plex_stream_libvlc_test.cpp)
    koalaamp_enable_warnings(plex_stream_libvlc_tests)
    target_link_libraries(plex_stream_libvlc_tests PRIVATE
        Catch2::Catch2WithMain koalaamp_plex koalaamp_vlc)
    catch_discover_tests(plex_stream_libvlc_tests)
endif()

# SV-1: the connection manager's persisted server list. Identity only — the store
# is documented to carry no tokens or passwords, so the test pins that too.
add_executable(server_book_tests server_book_test.cpp)
koalaamp_enable_warnings(server_book_tests)
target_include_directories(server_book_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/third_party/include")
target_link_libraries(server_book_tests PRIVATE Catch2::Catch2WithMain koalaamp_servers)
catch_discover_tests(server_book_tests)

# SV-6: OS keyring round-trip. Skips when no credential daemon is running.
add_executable(platform_secret_tests platform_secret_test.cpp)
koalaamp_enable_warnings(platform_secret_tests)
target_include_directories(platform_secret_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(platform_secret_tests PRIVATE Catch2::Catch2 koalaamp_servers Qt6::Core)
catch_discover_tests(platform_secret_tests PROPERTIES TIMEOUT 40)

# SC-1: client-side scrobbling. Known-answer MD5/api_sig, Last.fm request/response
# shapes, ListenBrainz payloads/headers, the policy matrix, and the persisted
# offline queue. Every request goes through a fake transport; no network.
add_executable(koalaamp_scrobble_tests scrobble_test.cpp)
koalaamp_enable_warnings(koalaamp_scrobble_tests)
target_include_directories(koalaamp_scrobble_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/third_party/include")
target_link_libraries(koalaamp_scrobble_tests PRIVATE
    Catch2::Catch2WithMain koalaamp_scrobble)
catch_discover_tests(koalaamp_scrobble_tests)

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

    # SV-2 direct play: libvlc decodes a Jellyfin item through
    # libvlc_media_new_callbacks (no temp file, token in the header only).
    add_executable(koalaamp_jellyfin_stream_tests jellyfin_stream_libvlc_test.cpp)
    koalaamp_enable_warnings(koalaamp_jellyfin_stream_tests)
    target_link_libraries(koalaamp_jellyfin_stream_tests PRIVATE
        Catch2::Catch2WithMain koalaamp_jellyfin koalaamp_vlc)
    catch_discover_tests(koalaamp_jellyfin_stream_tests)
    message(STATUS "KoalaAmp: Jellyfin callback-stream test enabled")

    # SV-2: remote commands against the real queue (PlayNext enqueues).
    add_executable(jellyfin_remote_playback_tests jellyfin_remote_playback_test.cpp)
    koalaamp_enable_warnings(jellyfin_remote_playback_tests)
    target_link_libraries(jellyfin_remote_playback_tests PRIVATE
        Catch2::Catch2WithMain koalaamp_jellyfin koalaamp_playback_core)
    catch_discover_tests(jellyfin_remote_playback_tests)
    message(STATUS "KoalaAmp: Jellyfin remote-queue test enabled")
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
target_compile_definitions(koalaamp_skins_tests PRIVATE
    "KOALAAMP_BUNDLED_SKINS=\"${CMAKE_SOURCE_DIR}/skins\"")
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

# AU-2: the gapless boundary (DSP path) + ReplayGain activation from real track
# tags. Both live on the libvlc-backed chain (the boundary is the core's, the RG
# stage is AU-1's transform installed at the core's seam), so the target exists
# where koalaamp_playback_core does.
if(TARGET koalaamp_playback_core AND TARGET koalaamp_dsp)
    add_executable(koalaamp_au2_tests gapless_rg_test.cpp)
    koalaamp_enable_warnings(koalaamp_au2_tests)
    target_link_libraries(koalaamp_au2_tests PRIVATE Catch2::Catch2WithMain koalaamp_dsp
        koalaamp_playback_core)
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/audio/RgTags.hpp")
        target_compile_definitions(koalaamp_au2_tests PRIVATE KOALAAMP_AU2_HAS_RGTAGS)
    endif()
    catch_discover_tests(koalaamp_au2_tests PROPERTIES TIMEOUT 900)
    message(STATUS "KoalaAmp: AU-2 gapless/ReplayGain tests enabled")
endif()

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

# SE-3: the classic playlist editor. The ATF/model suite and the render/golden
# suite need only Qt Gui; the interaction-parity suite drives the real widget
# (Qt Widgets + QtTest, offscreen, PRE_TEST discovery like SE-2); the corpus
# smoke renders all 30 corpus skins. `koalaamp_se3_goldens_gen` regenerates the
# committed golden manifest (hashes only) — see tests/se3_goldens_gen.cpp.
add_executable(koalaamp_se3_atf_tests se3_atf_test.cpp)
koalaamp_enable_warnings(koalaamp_se3_atf_tests)
target_link_libraries(koalaamp_se3_atf_tests PRIVATE Catch2::Catch2WithMain koalaamp_ui)
target_compile_definitions(koalaamp_se3_atf_tests PRIVATE
    "KOALAAMP_UI_SRC=\"${CMAKE_SOURCE_DIR}/src/ui\""
    "KOALAAMP_PLAYBACK_SRC=\"${CMAKE_SOURCE_DIR}/src/playback\""
    "KOALAAMP_INTEROP_SRC=\"${CMAKE_SOURCE_DIR}/src/interop\""
    "KOALAAMP_AUDIO_SRC=\"${CMAKE_SOURCE_DIR}/src/audio\"")
catch_discover_tests(koalaamp_se3_atf_tests)

add_executable(koalaamp_se3_render_tests se3_render_test.cpp)
koalaamp_enable_warnings(koalaamp_se3_render_tests)
target_link_libraries(koalaamp_se3_render_tests PRIVATE Catch2::Catch2 koalaamp_ui)
target_compile_definitions(koalaamp_se3_render_tests PRIVATE
    "KOALAAMP_SE3_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\""
    "KOALAAMP_SE3_GOLDEN_MANIFEST=\"${CMAKE_SOURCE_DIR}/tests/fixtures/se3/goldens.sha256\"")
catch_discover_tests(koalaamp_se3_render_tests PROPERTIES TIMEOUT 300)

add_executable(koalaamp_se3_interaction_tests se3_interaction_test.cpp)
koalaamp_enable_warnings(koalaamp_se3_interaction_tests)
target_link_libraries(koalaamp_se3_interaction_tests PRIVATE Catch2::Catch2 koalaamp_ui Qt6::Test)
target_compile_definitions(koalaamp_se3_interaction_tests PRIVATE
    "KOALAAMP_SE3_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\"")
catch_discover_tests(koalaamp_se3_interaction_tests PROPERTIES
    DISCOVERY_MODE PRE_TEST
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 180)

add_executable(koalaamp_se3_corpus_tests se3_corpus_render_test.cpp)
koalaamp_enable_warnings(koalaamp_se3_corpus_tests)
target_link_libraries(koalaamp_se3_corpus_tests PRIVATE Catch2::Catch2WithMain koalaamp_ui)
target_compile_definitions(koalaamp_se3_corpus_tests PRIVATE
    "KOALAAMP_CORPUS_DIR=\"${CMAKE_SOURCE_DIR}/third_party/skins-corpus\"")
catch_discover_tests(koalaamp_se3_corpus_tests PROPERTIES TIMEOUT 300)
message(STATUS "KoalaAmp: SE-3 playlist-window tests enabled (atf/model, render/goldens, interaction, corpus)")

# SE-3 queue semantics against the real core (gated on the libvlc-backed
# PlaybackCore bridge, exactly like SE-2's engine-dependent checks).
if(TARGET koalaamp_ui_playback)
    add_executable(koalaamp_se3_queue_tests se3_queue_test.cpp)
    koalaamp_enable_warnings(koalaamp_se3_queue_tests)
    target_link_libraries(koalaamp_se3_queue_tests PRIVATE Catch2::Catch2 koalaamp_ui_playback)
    catch_discover_tests(koalaamp_se3_queue_tests PROPERTIES TIMEOUT 120)
    message(STATUS "KoalaAmp: SE-3 real-queue transport test enabled")
endif()

# Developer tool: prints the SE-3 golden manifest. Excluded from the default
# build (regeneration is deliberate):
#   cmake --build build/linux-gcc --target koalaamp_se3_goldens_gen
add_executable(koalaamp_se3_goldens_gen EXCLUDE_FROM_ALL se3_goldens_gen.cpp)
koalaamp_enable_warnings(koalaamp_se3_goldens_gen)
target_link_libraries(koalaamp_se3_goldens_gen PRIVATE koalaamp_ui)

# SE-5: the classic equalizer window. The renderer/formula/golden suite and the
# 30-skin render smoke need only Qt Gui (koalaamp_ui carries the AU-1 seam); the
# interaction-parity suite drives the real widget (Qt Widgets + QtTest) and
# asserts the DSP wiring through AU-1's real Equalizer.
add_executable(koalaamp_se5_render_tests se5_render_test.cpp)
koalaamp_enable_warnings(koalaamp_se5_render_tests)
target_link_libraries(koalaamp_se5_render_tests PRIVATE Catch2::Catch2WithMain koalaamp_ui)
target_compile_definitions(koalaamp_se5_render_tests PRIVATE
    "KOALAAMP_SE5_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\""
    "KOALAAMP_SE5_GOLDEN_MANIFEST=\"${CMAKE_SOURCE_DIR}/tests/fixtures/se5/goldens.sha256\"")
catch_discover_tests(koalaamp_se5_render_tests)

add_executable(koalaamp_se5_interaction_tests se5_interaction_test.cpp)
koalaamp_enable_warnings(koalaamp_se5_interaction_tests)
target_link_libraries(koalaamp_se5_interaction_tests PRIVATE Catch2::Catch2 koalaamp_ui Qt6::Test)
target_compile_definitions(koalaamp_se5_interaction_tests PRIVATE
    "KOALAAMP_SE5_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\""
    "KOALAAMP_CORPUS_DIR=\"${CMAKE_SOURCE_DIR}/third_party/skins-corpus\""
    "KOALAAMP_EQ_FIXTURE_DIR=\"${CMAKE_SOURCE_DIR}/tests/fixtures/eq\"")
# Widget test: same PRE_TEST discovery + offscreen rationale as SE-2.
catch_discover_tests(koalaamp_se5_interaction_tests PROPERTIES
    DISCOVERY_MODE PRE_TEST
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 180)

add_executable(koalaamp_se5_corpus_tests se5_corpus_render_test.cpp)
koalaamp_enable_warnings(koalaamp_se5_corpus_tests)
target_link_libraries(koalaamp_se5_corpus_tests PRIVATE Catch2::Catch2WithMain koalaamp_ui)
target_compile_definitions(koalaamp_se5_corpus_tests PRIVATE
    "KOALAAMP_CORPUS_DIR=\"${CMAKE_SOURCE_DIR}/third_party/skins-corpus\"")
catch_discover_tests(koalaamp_se5_corpus_tests PROPERTIES TIMEOUT 300)
message(STATUS "KoalaAmp: SE-5 equalizer-window tests enabled (render/goldens, interaction, corpus)")

# SE-4: the application window set. Part A (this file) proves the assembled app:
# the XWayland launch decision, the WindowSet toggle routing (the EQ window is
# live), an EQ slider reaching AU-1's real DSP within one block through the
# production DspEqSink, and all three windows rendering with a skin offscreen.
# Widget cases use Qt Widgets + QtTest with the same PRE_TEST/offscreen setup.
add_executable(koalaamp_se4_window_set_tests se4_part_a_test.cpp)
koalaamp_enable_warnings(koalaamp_se4_window_set_tests)
target_link_libraries(koalaamp_se4_window_set_tests PRIVATE Catch2::Catch2 koalaamp_ui Qt6::Test)
target_compile_definitions(koalaamp_se4_window_set_tests PRIVATE
    "KOALAAMP_SE4_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\"")
catch_discover_tests(koalaamp_se4_window_set_tests PROPERTIES
    DISCOVERY_MODE PRE_TEST
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 180)
if(TARGET koalaamp_ui_playback)
    # Adds the real-core seam case (app chain -> setPcmCallback, spec §3.1 rule)
    # where the libvlc-backed core is built.
    target_link_libraries(koalaamp_se4_window_set_tests PRIVATE koalaamp_ui_playback)
    target_compile_definitions(koalaamp_se4_window_set_tests PRIVATE KOALAAMP_SE4_HAS_CORE)
endif()
message(STATUS "KoalaAmp: SE-4 Part A window-set tests enabled")

# SE-4 Part B: the snap/dock manager, keep-on-top and layout persistence.
# Scripted offscreen drags against the real widgets.
# MI-1: read-only Winamp NDE parser (generated fixtures + corruption smoke).
add_executable(migration_tests migration_test.cpp)
koalaamp_enable_warnings(migration_tests)
target_link_libraries(migration_tests PRIVATE Catch2::Catch2WithMain koalaamp_migration)
catch_discover_tests(migration_tests)

# LI-3: offscreen library-window render smoke across the corpus.
add_executable(koalaamp_li3_window_tests li3_window_test.cpp)
koalaamp_enable_warnings(koalaamp_li3_window_tests)
target_link_libraries(koalaamp_li3_window_tests PRIVATE
    Catch2::Catch2WithMain koalaamp_ui koalaamp_library)
target_compile_definitions(koalaamp_li3_window_tests PRIVATE
    "KOALAAMP_CORPUS_DIR=\"${CMAKE_SOURCE_DIR}/third_party/skins-corpus\"")
catch_discover_tests(koalaamp_li3_window_tests PROPERTIES
    DISCOVERY_MODE PRE_TEST
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 180)

add_executable(koalaamp_se4_snap_tests se4_snap_test.cpp)
koalaamp_enable_warnings(koalaamp_se4_snap_tests)
target_link_libraries(koalaamp_se4_snap_tests PRIVATE Catch2::Catch2 koalaamp_ui Qt6::Test)
catch_discover_tests(koalaamp_se4_snap_tests PROPERTIES
    DISCOVERY_MODE PRE_TEST
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 180)
message(STATUS "KoalaAmp: SE-4 Part B snap/dock tests enabled")

# VZ-1: the visualizer's PCM plumbing — the fan-out that gives the visualizer's
# ring a second reader of the DSP output (never a second setPcmCallback), and the
# stereo ring projectM is fed from. Qt-free logic; links the UI lib for the types.
add_executable(koalaamp_vz1_tests vz1_test.cpp)
koalaamp_enable_warnings(koalaamp_vz1_tests)
target_link_libraries(koalaamp_vz1_tests PRIVATE Catch2::Catch2WithMain koalaamp_ui)
catch_discover_tests(koalaamp_vz1_tests)
message(STATUS "KoalaAmp: VZ-1 visualizer plumbing tests enabled")

# VZ-1: the engine seam's headless contract — the factory, the no-GL-context failure
# path, and out-of-order driving. The real render check is not here on purpose: these
# tests run offscreen while GL needs a real server (FE-14 used xvfb-run + llvmpipe).
add_executable(koalaamp_vz1_engine_tests vz1_engine_test.cpp)
koalaamp_enable_warnings(koalaamp_vz1_engine_tests)
target_link_libraries(koalaamp_vz1_engine_tests PRIVATE Catch2::Catch2WithMain
    koalaamp_interop_projectm)
if(KOALAAMP_HAS_PROJECTM)
    # The vendored library is not on the loader's search path: an RPATH for the
    # ELF/Mach-O builds, and the DLL directory on PATH for Windows — needed at
    # ctest time, which is why discovery is deferred (below).
    set_target_properties(koalaamp_vz1_engine_tests PROPERTIES
        BUILD_RPATH "${KOALAAMP_PROJECTM_LIBDIR}")
    # PRE_TEST: build-time discovery executes the exe where no test ENVIRONMENT
    # applies, and on Windows it needs libprojectM-4.dll on PATH (0xc0000135). The CI
    # job provides that path, the same way it already provides Qt's and vcpkg's.
    catch_discover_tests(koalaamp_vz1_engine_tests PROPERTIES
        DISCOVERY_MODE PRE_TEST)
else()
    catch_discover_tests(koalaamp_vz1_engine_tests)
endif()
message(STATUS "KoalaAmp: VZ-1 engine seam tests enabled")

# VZ-1: the window side - the GL canvas that hosts the engine. Offscreen like the
# other widget suites (no context is created here), so the render path is verified
# separately under xvfb-run, the way FE-14 did it. Only exists when the canvas does.
if(KOALAAMP_HAS_GL_VISUALIZER)
    # SV-5: the server browser window against the fixture transport.
    add_executable(jellyfin_browser_window_tests server_browser_test.cpp)
    koalaamp_enable_warnings(jellyfin_browser_window_tests)
    target_link_libraries(jellyfin_browser_window_tests PRIVATE
        Catch2::Catch2 koalaamp_ui Qt6::Test)
    target_compile_definitions(jellyfin_browser_window_tests PRIVATE
        KOALAAMP_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/jellyfin"
        KOALAAMP_PLEX_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/plex")
    catch_discover_tests(jellyfin_browser_window_tests PROPERTIES
        DISCOVERY_MODE PRE_TEST
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120)

    add_executable(koalaamp_vz1_window_tests vz1_window_test.cpp)
    koalaamp_enable_warnings(koalaamp_vz1_window_tests)
    # The test constructs the engine itself (as the app will), so it links the
    # faade and needs the vendored .so on its RPATH; the UI lib deliberately does not.
    target_link_libraries(koalaamp_vz1_window_tests PRIVATE Catch2::Catch2 koalaamp_ui
        koalaamp_interop_projectm)
    if(KOALAAMP_HAS_PROJECTM)
        set_target_properties(koalaamp_vz1_window_tests PROPERTIES
            BUILD_RPATH "${KOALAAMP_PROJECTM_LIBDIR}")
        # PRE_TEST for the same reason as the SE-2 widget suites (build-time
        # discovery runs the exe where no test ENVIRONMENT applies), and because on
        # Windows the vendored projectM DLL is only on the job's PATH.
        catch_discover_tests(koalaamp_vz1_window_tests PROPERTIES
            DISCOVERY_MODE PRE_TEST
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    else()
        catch_discover_tests(koalaamp_vz1_window_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    message(STATUS "KoalaAmp: VZ-1 window tests enabled")
endif()

# VZ-1: the visualizer window itself - the gen frame (skinned, or the designed
# fallback), the canvas host, Winamp close/drag/scale, and the no-projectM state.
# Offscreen like the other widget suites; the real GL render is FE-14's xvfb path.
if(KOALAAMP_HAS_GL_VISUALIZER)
    add_executable(koalaamp_vz1_viswindow_tests vz1_visualizer_window_test.cpp)
    koalaamp_enable_warnings(koalaamp_vz1_viswindow_tests)
    target_link_libraries(koalaamp_vz1_viswindow_tests PRIVATE Catch2::Catch2WithMain koalaamp_ui
        koalaamp_interop_projectm)
    target_compile_definitions(koalaamp_vz1_viswindow_tests PRIVATE
        "KOALAAMP_SE2_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\""
        "KOALAAMP_CORPUS_DIR=\"${CMAKE_SOURCE_DIR}/third_party/skins-corpus\"")
    if(KOALAAMP_HAS_PROJECTM)
        set_target_properties(koalaamp_vz1_viswindow_tests PROPERTIES
            BUILD_RPATH "${KOALAAMP_PROJECTM_LIBDIR}")
        catch_discover_tests(koalaamp_vz1_viswindow_tests PROPERTIES
            DISCOVERY_MODE PRE_TEST
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    else()
        catch_discover_tests(koalaamp_vz1_viswindow_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    message(STATUS "KoalaAmp: VZ-1 visualizer window tests enabled")

    # SV-1: the connection-manager dialog — signing in through the real client path
    # (fixture transport) into a temporary book, plus the no-credential-on-disk rule.
    add_executable(connection_dialog_tests connection_dialog_test.cpp)
    koalaamp_enable_warnings(connection_dialog_tests)
    target_link_libraries(connection_dialog_tests PRIVATE Catch2::Catch2WithMain koalaamp_ui
        koalaamp_servers koalaamp_jellyfin)
    target_compile_definitions(connection_dialog_tests PRIVATE
        KOALAAMP_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/jellyfin")
    catch_discover_tests(connection_dialog_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "KoalaAmp: SV-1 connection-manager tests enabled")

    # MI-1: the three-step import wizard's path validation and option routing.
    add_executable(migration_dialog_tests migration_dialog_test.cpp)
    koalaamp_enable_warnings(migration_dialog_tests)
    target_link_libraries(migration_dialog_tests PRIVATE Catch2::Catch2WithMain koalaamp_ui
        koalaamp_migration)
    catch_discover_tests(migration_dialog_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "KoalaAmp: MI-1 migration-wizard tests enabled")
endif()

# VZ-1 task 3: the preset library - the recursive scan, the watcher, and the
# ordered/random selection. Qt Core only, so it needs no GL and no platform plugin.
add_executable(koalaamp_vz1_preset_tests vz1_preset_store_test.cpp)
koalaamp_enable_warnings(koalaamp_vz1_preset_tests)
target_link_libraries(koalaamp_vz1_preset_tests PRIVATE Catch2::Catch2WithMain koalaamp_ui)
catch_discover_tests(koalaamp_vz1_preset_tests)
message(STATUS "KoalaAmp: VZ-1 preset store tests enabled")

# PL-1: playlist formats. The unit suite (round-trip, byte-exact M3U8 emission,
# leniency corpus, index, autosave) and the fuzz pass are Qt-free and link only
# the Qt-free `koalaamp_playlist` module — the fuzz binary is the ASAN/UBSAN
# target (AGENTS.md requires it for parser code). The window suite drives the
# real SE-3 widget (open/save verbs, drag-drop, autosave restore) offscreen.
add_executable(koalaamp_playlist_tests playlist_test.cpp)
koalaamp_enable_warnings(koalaamp_playlist_tests)
target_include_directories(koalaamp_playlist_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(koalaamp_playlist_tests PRIVATE Catch2::Catch2WithMain koalaamp_playlist)
target_compile_definitions(koalaamp_playlist_tests PRIVATE
    "KOALAAMP_PLAYLIST_FIXTURE_DIR=\"${CMAKE_SOURCE_DIR}/tests/fixtures/playlist\""
    "KOALAAMP_PLAYLIST_SRC=\"${CMAKE_SOURCE_DIR}/src/playlist\"")
catch_discover_tests(koalaamp_playlist_tests PROPERTIES TIMEOUT 300)
message(STATUS "KoalaAmp: PL-1 playlist-format unit tests enabled")

add_executable(koalaamp_playlist_fuzz_tests playlist_fuzz_test.cpp)
koalaamp_enable_warnings(koalaamp_playlist_fuzz_tests)
target_include_directories(koalaamp_playlist_fuzz_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(koalaamp_playlist_fuzz_tests PRIVATE Catch2::Catch2WithMain koalaamp_playlist)
target_compile_definitions(koalaamp_playlist_fuzz_tests PRIVATE
    "KOALAAMP_PLAYLIST_FIXTURE_DIR=\"${CMAKE_SOURCE_DIR}/tests/fixtures/playlist\"")
catch_discover_tests(koalaamp_playlist_fuzz_tests PROPERTIES TIMEOUT 600)
message(STATUS "KoalaAmp: PL-1 playlist parser fuzz target enabled (ASAN/UBSAN)")

add_executable(koalaamp_pl1_window_tests pl1_window_test.cpp)
koalaamp_enable_warnings(koalaamp_pl1_window_tests)
target_include_directories(koalaamp_pl1_window_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(koalaamp_pl1_window_tests PRIVATE Catch2::Catch2 koalaamp_ui Qt6::Test)
catch_discover_tests(koalaamp_pl1_window_tests PROPERTIES
    DISCOVERY_MODE PRE_TEST
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 180)
message(STATUS "KoalaAmp: PL-1 playlist-window wiring tests enabled")
