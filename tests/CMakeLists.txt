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

# FE-3: WSZ/BMP decode + ported sprite table. Decoder cases use synthetic BMPs;
# the base-2.91 cases skip when the gitignored fixture has not been fetched
# (spikes/fe3_wsz/fetch-skin.sh).
if(TARGET koalaamp_fe3_wsz)
    add_executable(koalaamp_fe3_tests fe3_wsz_test.cpp)
    koalaamp_enable_warnings(koalaamp_fe3_tests)
    target_link_libraries(koalaamp_fe3_tests PRIVATE Catch2::Catch2WithMain koalaamp_fe3_wsz)
    target_compile_definitions(koalaamp_fe3_tests PRIVATE
        "KOALAAMP_FE3_SKIN=\"${CMAKE_SOURCE_DIR}/third_party/skins/base-2.91.wsz\"")
    # Windows: the test exe links Qt DLLs; Catch2's discovery step runs it at
    # build time and needs them on PATH (0xc0000135 otherwise). install-qt-action
    # puts the DLLs in the Qt bin dir.
    if(WIN32)
        catch_discover_tests(koalaamp_fe3_tests
            ENVIRONMENT "PATH=$<SHELL_PATH:$<TARGET_RUNTIME_DLL_DIRS:koalaamp_fe3_tests>;$ENV:PATH>")
    else()
        catch_discover_tests(koalaamp_fe3_tests)
    endif()
    message(STATUS "KoalaAmp: FE-3 WSZ decode/table tests enabled")
endif()
