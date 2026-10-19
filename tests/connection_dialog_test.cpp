// SPDX-License-Identifier: MIT
// SV-1 — the connection manager dialog: signing in through the real client path
// (fixture transport), the book it writes (identity only), and removal.
//
// The auth-failure mapping is already covered by `jellyfin_port_tests`; this case
// pins the dialog's wiring, which is where a credential could accidentally leak to
// disk.

#include "servers/ServerBook.hpp"
#include "servers/jellyfin/FixtureTransport.hpp"
#include "ui/ConnectionDialog.hpp"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;
using koalaamp::servers::ServerBook;
using koalaamp::servers::jellyfin::FixtureTransport;
using koalaamp::ui::ConnectionDialog;

fs::path freshDir() {
    const auto dir = fs::temp_directory_path() / "koalaamp-connection-dialog-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void setField(QWidget& dialog, const char* name, const std::string& text) {
    auto* field = dialog.findChild<QLineEdit*>(QString::fromLatin1(name));
    REQUIRE(field != nullptr);
    field->setText(QString::fromStdString(text));
}

void click(QWidget& dialog, const char* name) {
    auto* button = dialog.findChild<QPushButton*>(QString::fromLatin1(name));
    REQUIRE(button != nullptr);
    button->click();
}

} // namespace

TEST_CASE("signing in records the server and keeps the token in memory") {
    const auto dir = freshDir();
    ServerBook book(dir / "servers.json");
    FixtureTransport transport{std::filesystem::path(KOALAAMP_FIXTURE_DIR)};

    ConnectionDialog dialog(transport, book, (dir / "machine-id").string());
    setField(dialog, "urlField", "http://jellyfin.local:8096");
    setField(dialog, "userField", "tester");
    setField(dialog, "passwordField", "correct-horse");
    click(dialog, "loginButton");

    REQUIRE(dialog.session().has_value());
    CHECK(dialog.session()->accessToken == "fixture-token-1");
    CHECK(dialog.session()->userId == "11111111111111111111111111111111");
    CHECK_FALSE(dialog.session()->deviceId.empty());

    REQUIRE(book.entries().size() == 1);
    const auto& entry = book.entries().front();
    CHECK(entry.serverId == "0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
    CHECK(entry.baseUrl == "http://jellyfin.local:8096");
    CHECK(entry.userName == "tester");
    REQUIRE(book.active() != nullptr);
    CHECK(book.active()->serverId == entry.serverId);

    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("serverList"));
    REQUIRE(list != nullptr);
    CHECK(list->count() == 1);

    // The token is in memory only; the saved book carries identity.
    std::ifstream in(dir / "servers.json");
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    CHECK(text.find("fixture-token-1") == std::string::npos);
    CHECK(text.find("token") == std::string::npos);

    // Removing the only server clears the session and the book.
    click(dialog, "removeButton");
    CHECK_FALSE(dialog.session().has_value());
    CHECK(book.entries().empty());
    CHECK(book.active() == nullptr);

    fs::remove_all(dir);
}

TEST_CASE("a scheme-less server URL is treated as http") {
    const auto dir = freshDir();
    ServerBook book(dir / "servers.json");
    FixtureTransport transport{std::filesystem::path(KOALAAMP_FIXTURE_DIR)};

    ConnectionDialog dialog(transport, book, (dir / "machine-id").string());
    setField(dialog, "urlField", "jellyfin.local:8096"); // what people type
    setField(dialog, "userField", "tester");
    setField(dialog, "passwordField", "correct-horse");
    click(dialog, "loginButton");

    // The fixture only answers the http:// URL, so a successful login also proves the
    // scheme was added; the stored base URL records the normalised form.
    REQUIRE(dialog.session().has_value());
    REQUIRE(book.entries().size() == 1);
    CHECK(book.entries().front().baseUrl == "http://jellyfin.local:8096");

    fs::remove_all(dir);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
