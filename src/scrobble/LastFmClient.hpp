// SPDX-License-Identifier: MIT
// scrobble — Last.fm API 2.0 client (desktop auth + scrobbling).
//
// Wire contract verified against the public docs, 2026-10:
//   https://www.last.fm/api/desktopauth
//   https://www.last.fm/api/authspec        (signing: sort name+value, append secret, MD5)
//   https://www.last.fm/api/show/auth.getToken
//   https://www.last.fm/api/show/auth.getSession
//   https://www.last.fm/api/show/track.updateNowPlaying
//   https://www.last.fm/api/show/track.scrobble
//
// Every call is a POST with the parameters in the form body: the session key and
// the shared secret never appear in a URL, and nothing is logged.
//
// koalapaw has no Last.fm/ListenBrainz client (its scrobble calls are the Plex
// `/:/scrobble` ones), so there is no Kotlin source to cite for these shapes;
// the docs above are the source of truth.

#pragma once

#include "scrobble/ScrobbleSubmitter.hpp"
#include "scrobble/Transport.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace koalaamp::scrobble {

struct LastFmCredentials {
    std::string apiKey;
    std::string sharedSecret;
};

struct LastFmSession {
    std::string name;
    std::string key;
    bool subscriber{false};
};

struct LastFmError {
    /// Last.fm's numeric error (6 = invalid parameters, 9 = invalid session key,
    /// 13 = bad signature, …). 0 for transport/local failures.
    int code{0};
    long httpStatus{0};
    std::string message;
    /// True when no HTTP response was produced.
    bool transport{false};

    bool ok() const { return code == 0 && !transport && message.empty(); }
};

struct LastFmScrobbleResult {
    int accepted{0};
    int ignored{0};
};

/// The browser URL for the desktop-auth confirmation step (section 3 of the
/// how-to). It carries the API key and the request token — never the shared
/// secret or a session key.
std::string lastFmAuthUrl(std::string_view apiKey, std::string_view requestToken);

class LastFmClient final : public ILastFmSubmitter {
public:
    LastFmClient(LastFmCredentials credentials, ITransport& transport,
                 std::string baseUrl = "https://ws.audioscrobbler.com/2.0/");

    /// The session key from auth.getSession, normally restored from the OS
    /// keychain (`loadLastFmSessionKey`).
    void setSessionKey(std::string sessionKey);
    const std::string& sessionKey() const { return sessionKey_; }

    std::optional<std::string> getToken();
    std::optional<LastFmSession> getSession(std::string_view token);

    bool updateNowPlaying(const ScrobbleTrack& track) override;
    /// One POST, array notation (`artist[0]`, `track[0]`, …), max 50 listens.
    bool scrobble(const std::vector<TimedScrobble>& batch) override;

    /// Filled by the last successful `scrobble`.
    LastFmScrobbleResult lastScrobbleResult() const { return lastScrobbleResult_; }
    const LastFmError& lastError() const { return lastError_; }

private:
    using Params = std::vector<std::pair<std::string, std::string>>;

    /// MD5 over the parameters sorted ASCII-wise by name (format/callback
    /// excluded), each as name+value, with the shared secret appended.
    std::string sign(Params params) const;
    HttpResponse post(Params params);
    void clearError();

    LastFmCredentials credentials_;
    ITransport& transport_;
    std::string baseUrl_;
    std::string sessionKey_;
    LastFmError lastError_;
    LastFmScrobbleResult lastScrobbleResult_;
};

} // namespace koalaamp::scrobble
