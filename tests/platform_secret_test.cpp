// SPDX-License-Identifier: MIT
// SV-6 — the OS keyring round-trip. Skips where no credential service is running
// (CI without a session bus). The dialog tests cover the policy with a memory store.

#include "servers/SecretStore.hpp"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>

#include <random>
#include <string>

TEST_CASE("the OS keyring round-trips a secret") {
    auto store = koalaamp::servers::makePlatformSecretStore();
    std::mt19937_64 rng{std::random_device{}()};
    const std::string key = "jellyfin/koalaamp-selftest-" + std::to_string(rng());
    struct Guard {
        koalaamp::servers::SecretStore* store;
        std::string key;
        ~Guard() { store->remove(key); }
    } guard{store.get(), key};

    if (!store->store(key, "secret-value")) {
        SKIP("no OS keyring available");
    }
    const auto loaded = store->load(key);
    REQUIRE(loaded.has_value());
    CHECK(*loaded == "secret-value");
    CHECK(store->remove(key));
    CHECK_FALSE(store->load(key).has_value());
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
