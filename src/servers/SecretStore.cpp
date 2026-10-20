// SPDX-License-Identifier: MIT
// servers — session token bookkeeping on top of a SecretStore. See SecretStore.hpp.

#include "servers/SecretStore.hpp"

namespace koalaamp::servers {

std::string sessionSecretKey(ServerBackend backend, std::string_view serverId,
                             std::string_view baseUrl) {
    const std::string_view id = serverId.empty() ? baseUrl : serverId;
    return std::string(backend == ServerBackend::Plex ? "plex/" : "jellyfin/") + std::string(id);
}

bool rememberSession(SecretStore& store, const ServerAuth& auth) {
    if (auth.accessToken.empty()) {
        return false;
    }
    return store.store(sessionSecretKey(auth.backend, auth.serverId, auth.baseUrl),
                       auth.accessToken);
}

void forgetSession(SecretStore& store, ServerBackend backend, std::string_view serverId,
                   std::string_view baseUrl) {
    store.remove(sessionSecretKey(backend, serverId, baseUrl));
}

std::optional<ServerAuth> restoreSession(const ServerBook& book, const SecretStore& store) {
    const ServerEntry* active = book.active();
    if (active == nullptr) {
        return std::nullopt;
    }
    const auto token =
        store.load(sessionSecretKey(active->backend, active->serverId, active->baseUrl));
    if (!token || token->empty()) {
        return std::nullopt;
    }
    ServerAuth auth;
    auth.baseUrl = active->baseUrl;
    auth.userId = active->userId;
    auth.accessToken = *token;
    auth.deviceId = active->deviceId;
    auth.serverId = active->serverId;
    auth.backend = active->backend;
    auth.userName = active->userName;
    return auth;
}

} // namespace koalaamp::servers
