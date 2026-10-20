// SPDX-License-Identifier: MIT
// scrobble — production HTTP transport factory.
//
// The public header names no Jellyfin type: the adapter's implementation wraps
// the existing libcurl transport (`koalaamp::servers::jellyfin::CurlTransport`)
// and converts requests/responses. That keeps the scrobble module's public
// headers Qt-free and gateway-free while avoiding a second curl stack. The UI
// composition root installs this; tests inject a fake ITransport instead.

#pragma once

#include "scrobble/Transport.hpp"

#include <chrono>
#include <memory>

namespace koalaamp::scrobble {

/// libcurl-backed transport. Defaults match the Jellyfin gateway's (5 s connect,
/// 30 s total); a scrobble POST is small, and the queue retries on failure.
std::unique_ptr<ITransport> makeCurlTransport(
    std::chrono::milliseconds connectTimeout = std::chrono::seconds(5),
    std::chrono::milliseconds totalTimeout = std::chrono::seconds(30));

} // namespace koalaamp::scrobble
