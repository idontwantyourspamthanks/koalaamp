// SPDX-License-Identifier: MIT
//
// Atf — the Winamp "Advanced Title Formatting" subset the playlist uses
// (docs/spec.md §11): `%artist%`/`%title%`/`%album%`/`%filename%`, the
// `[optional block]`, `$if2(a,b,...)` and `$filepart(x)` functions, and the
// classic default
//     [%artist% - ]$if2(%title%,$filepart(%filename%))
//
// The formatter is a pure function of (format, fields). Behaviour is a subset
// of Winamp's ATF, documented black-box; the official ATF reference is the
// `gen_atf` plugin help, not consulted as source.
//
// TAG-SOURCE SEAM (LI-1): for now the only tag source is the file name — the
// `title` field is the file stem and `artist`/`album` are empty, so the default
// format renders exactly the filename stem. LI-1's metadata API fills these
// fields from real tags; `atfFieldsFor()` is the single place that will change.
#pragma once

#include <string>
#include <string_view>

#include "playback/IAudioEngine.hpp"

namespace koalaamp::ui {

/// The values an ATF format can reference.
struct AtfFields {
    std::string artist;
    std::string title;
    std::string album;
    std::string filename;  ///< full path or URL as handed to the transport
};

/// The spec §11 default (Winamp's own `titlefmt` default).
inline constexpr std::string_view kDefaultAtf =
    "[%artist% - ]$if2(%title%,$filepart(%filename%))";

/// Evaluates `format` against `fields`. Total: malformed input yields the
/// literal text, never an exception.
std::string formatAtf(std::string_view format, const AtfFields& fields);

/// Fills `AtfFields` from a media reference. File stem only for now (LI-1 seam).
AtfFields atfFieldsFor(const playback::MediaRef& media);

/// The default display title for a media: `formatAtf(kDefaultAtf, ...)`.
std::string defaultPlaylistTitle(const playback::MediaRef& media);

/// The final path segment of a path or URL (ATF `$filepart`).
std::string filePart(std::string_view path);

}  // namespace koalaamp::ui
