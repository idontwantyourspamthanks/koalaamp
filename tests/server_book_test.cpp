// SPDX-License-Identifier: MIT
// SV-1 — the connection manager's persisted server list.
//
// Identity only: the store must never grow a credential field, so the last case
// pins the serialised form rather than trusting the struct.

#include "servers/ServerBook.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
namespace fs = std::filesystem;
using koalaamp::servers::ServerBook;
using koalaamp::servers::ServerEntry;

fs::path freshDir() {
    const auto dir = fs::temp_directory_path() / "koalaamp-server-book-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

ServerEntry entry(const char* id, const char* name, const char* url, const char* user) {
    ServerEntry value;
    value.serverId = id;
    value.name = name;
    value.baseUrl = url;
    value.userId = user;
    value.userName = user;
    value.deviceId = std::string{"device-"} + user;
    return value;
}

} // namespace

TEST_CASE("a saved server book round-trips, active included") {
    const auto dir = freshDir();
    const auto file = dir / "servers.json";
    {
        ServerBook book(file);
        REQUIRE(book.entries().empty());
        book.upsert(entry("s1", "Living room", "http://jellyfin.local:8096/", "tester"));
        REQUIRE(book.active() != nullptr);
        CHECK(book.active()->serverId == "s1");
        REQUIRE(book.save());
    }

    ServerBook reloaded(file);
    REQUIRE(reloaded.load());
    REQUIRE(reloaded.entries().size() == 1);
    // The trailing slash is trimmed on upsert, so the stored URL is the base.
    CHECK(reloaded.entries().front().baseUrl == "http://jellyfin.local:8096");
    REQUIRE(reloaded.active() != nullptr);
    CHECK(reloaded.active()->serverId == "s1");

    fs::remove_all(dir);
}

TEST_CASE("upsert replaces by server id and makes the entry active") {
    const auto dir = freshDir();
    ServerBook book(dir / "servers.json");
    book.upsert(entry("s1", "One", "http://one", "a"));
    book.upsert(entry("s2", "Two", "http://two", "b"));
    REQUIRE(book.entries().size() == 2);
    REQUIRE(book.active() != nullptr);
    CHECK(book.active()->serverId == "s2");

    book.upsert(entry("s1", "One renamed", "http://one", "a"));
    REQUIRE(book.entries().size() == 2);
    CHECK(book.entries().front().name == "One renamed");
    REQUIRE(book.active() != nullptr);
    CHECK(book.active()->serverId == "s1");

    fs::remove_all(dir);
}

TEST_CASE("removing the active server clears the selection") {
    const auto dir = freshDir();
    ServerBook book(dir / "servers.json");
    book.upsert(entry("s1", "One", "http://one", "a"));
    REQUIRE(book.setActive("s1"));
    CHECK(book.remove("s1"));
    CHECK(book.entries().empty());
    CHECK(book.active() == nullptr);
    CHECK_FALSE(book.remove("s1"));
    CHECK_FALSE(book.setActive("s1"));

    fs::remove_all(dir);
}

TEST_CASE("a malformed book loads empty and reports why") {
    const auto dir = freshDir();
    const auto file = dir / "servers.json";
    {
        std::ofstream out(file);
        out << "{ not json";
    }

    ServerBook book(file);
    CHECK_FALSE(book.load());
    CHECK(book.entries().empty());
    CHECK(book.active() == nullptr);
    CHECK_FALSE(book.lastError().empty());

    fs::remove_all(dir);
}

TEST_CASE("the store never writes credentials") {
    const auto dir = freshDir();
    const auto file = dir / "servers.json";
    ServerBook book(file);
    book.upsert(entry("s1", "One", "http://one", "a"));
    REQUIRE(book.save());

    std::ifstream in(file);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    // A future "just persist the token" change fails here rather than shipping.
    CHECK(text.find("token") == std::string::npos);
    CHECK(text.find("Token") == std::string::npos);
    CHECK(text.find("password") == std::string::npos);
    CHECK(text.find("\"Pw\"") == std::string::npos);

    fs::remove_all(dir);
}
