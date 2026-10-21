// SPDX-License-Identifier: MIT
// CO-1 — pure association manifest generation (Windows commands, Linux desktop
// entry, MIME mapping). The install paths write user state and are covered by
// the platform smoke/manual checks, not by ctest.

#include "platform/FileAssociations.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace koalaamp::platform;

TEST_CASE("association manifests name both verbs and the supported MIME types") {
    CHECK(mediaExtensions().contains("mp3"));
    CHECK(mediaExtensions().contains("m3u8"));
    CHECK(mimeTypeForExtension("MP3") == "audio/mpeg");
    CHECK(mimeTypeForExtension("flac") == "audio/flac");
    CHECK(mimeTypeForExtension("m3u") == "audio/x-mpegurl");
    CHECK(mimeTypeForExtension("unknown").isEmpty());

    CHECK(windowsPlayCommand("C:/KoalaAmp/koalaamp.exe").contains("--play \"%1\""));
    CHECK(windowsEnqueueCommand("C:/KoalaAmp/koalaamp.exe").contains("--enqueue \"%1\""));
}

TEST_CASE("Linux desktop entry carries both actions and audio MIME types") {
    const QString entry = linuxDesktopEntry("/opt/koalaamp/koalaamp", mediaExtensions());
    CHECK(entry.contains("Type=Application"));
    CHECK(entry.contains("\"/opt/koalaamp/koalaamp\" %F"));
    CHECK(entry.contains("audio/flac;"));
    CHECK(entry.contains("Actions=Play;Enqueue;"));
    CHECK(entry.contains("[Desktop Action Play]"));
    CHECK(entry.contains("[Desktop Action Enqueue]"));
    CHECK(entry.contains("--play %F"));
    CHECK(entry.contains("--enqueue %F"));
}
