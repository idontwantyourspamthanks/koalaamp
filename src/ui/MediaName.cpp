// SPDX-License-Identifier: MIT
#include "ui/MediaName.hpp"

namespace koalaamp::ui {

namespace {

/// Part after the last '/' or '\\' (URLs and Windows paths alike).
std::string lastSegment(const std::string& value) {
    const auto pos = value.find_last_of("/\\");
    if (pos == std::string::npos) {
        return value;
    }
    return value.substr(pos + 1);
}

}  // namespace

std::string mediaDisplayName(const playback::MediaRef& media) {
    std::string name = lastSegment(media.value);
    if (name.empty()) {
        return media.value;  // a URL ending in '/': show what we were given
    }
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        name.resize(dot);
    }
    return name;
}

}  // namespace koalaamp::ui
