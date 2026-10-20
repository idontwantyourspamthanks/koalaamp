// SPDX-License-Identifier: MIT
// servers — no OS keyring linked into this build. Sign-in stays in memory.

#include "servers/SecretStore.hpp"

namespace koalaamp::servers {
namespace {

class UnavailableStore final : public SecretStore {
public:
    bool store(std::string_view, std::string_view) override { return false; }
    std::optional<std::string> load(std::string_view) const override { return std::nullopt; }
    bool remove(std::string_view) override { return false; }
};

} // namespace

std::unique_ptr<SecretStore> makePlatformSecretStore() {
    return std::make_unique<UnavailableStore>();
}

} // namespace koalaamp::servers
