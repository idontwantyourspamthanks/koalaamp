// SPDX-License-Identifier: MIT
// Skeleton unit tests. These exercise the module interface headers (include
// paths, linking, C++20) — enough to prove the harness end-to-end.

#include "library/ITrackSource.hpp"
#include "playback/IAudioEngine.hpp"
#include "servers/IServerSource.hpp"
#include "skins/ISkinLoader.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Track defaults describe a local, rating-less item") {
    koalaamp::library::Track track;
    track.title = "Sample";
    REQUIRE(track.src == koalaamp::library::SourceId::Local);
    REQUIRE(track.title == "Sample");
    REQUIRE(track.rating == 0);
    REQUIRE_FALSE(track.rg.has_value());
}

TEST_CASE("MediaRef defaults to a filesystem path") {
    koalaamp::playback::MediaRef ref;
    REQUIRE(ref.kind == koalaamp::playback::MediaRef::Kind::Path);
    REQUIRE(ref.value.empty());
}

TEST_CASE("server and skin interfaces are polymorphic seams") {
    STATIC_REQUIRE(std::has_virtual_destructor_v<koalaamp::servers::IServerSource>);
    STATIC_REQUIRE(std::has_virtual_destructor_v<koalaamp::skins::ISkinLoader>);
    STATIC_REQUIRE(std::has_virtual_destructor_v<koalaamp::library::ITrackSource>);
}
