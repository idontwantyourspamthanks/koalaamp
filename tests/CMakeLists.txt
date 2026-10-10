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

# Headless smoke test: launch the real application, prove the frameless toplevel
# constructs and maps, then exit 0. Works on every desktop platform via Qt's
# offscreen platform plugin.
add_test(NAME app_smoke COMMAND $<TARGET_FILE:koalaamp> --self-test)
set_tests_properties(app_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30)
