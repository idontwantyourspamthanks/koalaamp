// SPDX-License-Identifier: MIT
// jellyfin — the `IServerSource` implementation (SV-5, closing SV-1's seam).
//
// One signed-in server exposed as a track source: search, stream (through the
// SV-2 callback stream) and rate. The token lives in `ServerAuth`, in memory
// only; `signOut` drops it. `libraryId` optionally scopes search to a music
// library picked in the browser (empty = whole server).

#pragma once

#include "servers/IServerSource.hpp"
#include "servers/jellyfin/AuthHeaders.hpp"
#include "servers/jellyfin/HttpTransport.hpp"
#include "servers/jellyfin/JellyfinClient.hpp"

#include <memory>
#include <string>

namespace koalaamp::servers::jellyfin {

class JellyfinSource final : public koalaamp::servers::IServerSource {
public:
    JellyfinSource(IHttpTransport& transport, DeviceProfile profile,
                   koalaamp::servers::ServerAuth auth);

    // --- ITrackSource --------------------------------------------------------
    std::vector<library::Track> search(const library::Query& query,
                                       library::Cancellation& cancel) override;
    std::unique_ptr<std::istream> openStream(const library::Track& track,
                                             library::StreamIntent intent) override;
    void setRating(const library::Track& track, int stars) override;

    // --- IServerSource -------------------------------------------------------
    bool isAuthenticated() const override;
    void signOut() override;

    /// Replace the session after a re-auth (token refresh, 401 recovery).
    void setSession(koalaamp::servers::ServerAuth auth);
    const koalaamp::servers::ServerAuth& session() const { return auth_; }

    /// Search scope: a music library id from `musicLibraries`, or empty.
    void setLibraryId(std::string libraryId) { libraryId_ = std::move(libraryId); }
    const std::string& libraryId() const { return libraryId_; }

    /// Why the last operation returned nothing (empty after success).
    const std::string& lastError() const { return lastError_; }

private:
    IHttpTransport& transport_;
    DeviceProfile profile_;
    koalaamp::servers::ServerAuth auth_;
    JellyfinClient client_;
    std::string libraryId_;
    std::string lastError_;
};

} // namespace koalaamp::servers::jellyfin
