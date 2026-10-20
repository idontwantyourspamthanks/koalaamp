// SPDX-License-Identifier: MIT
// Empty POST must not read stdin.
//
// QuickConnect's Initiate (`POST /QuickConnect/Initiate`) has no body. libcurl's
// default for CURLOPT_POST without a posted buffer is to read the body from
// stdin, and CURLOPT_TIMEOUT does not interrupt that read. From a terminal the
// button stays on "QuickConnect…" until the user types EOF. This test holds
// stdin open with no EOF, so a regression fails instead of hanging the suite.

#include "servers/jellyfin/HttpTransport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

using koalaamp::servers::jellyfin::CurlTransport;
using koalaamp::servers::jellyfin::HttpRequest;

#ifndef _WIN32

bool headerEquals(const std::string& request, const char* name, const char* value) {
    std::string lower = request;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    const std::string key = std::string(name) + ":";
    const auto at = lower.find(key);
    if (at == std::string::npos) {
        return false;
    }
    auto start = at + key.size();
    while (start < lower.size() && (lower[start] == ' ' || lower[start] == '\t')) {
        ++start;
    }
    const auto end = lower.find("\r\n", start);
    const auto got = lower.substr(start, end == std::string::npos ? std::string::npos : end - start);
    return got == value;
}

/// Answers one HTTP request on 127.0.0.1. A zero-length POST is answered at
/// once; anything else is left unread so a client blocked on stdin cannot
/// finish by accident when the server replies early (Expect: 100-continue).
class OneShotServer {
public:
    OneShotServer() {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(listenFd_ >= 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        REQUIRE(::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listenFd_, 1) == 0);
        socklen_t len = sizeof(addr);
        REQUIRE(::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_ = ntohs(addr.sin_port);

        thread_ = std::thread([this] { serve(); });
    }

    ~OneShotServer() {
        if (listenFd_ >= 0) {
            ::close(listenFd_);
            listenFd_ = -1;
        }
        join();
    }

    void join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    int port() const { return port_; }
    const std::string& request() const { return request_; }

private:
    void serve() {
        pollfd pfd{};
        pfd.fd = listenFd_;
        pfd.events = POLLIN;
        if (::poll(&pfd, 1, 4000) <= 0) {
            return;
        }
        const int client = ::accept(listenFd_, nullptr, nullptr);
        if (client < 0) {
            return;
        }
        timeval tv{};
        tv.tv_sec = 4;
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        char buf[2048];
        while (request_.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = ::recv(client, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            request_.append(buf, static_cast<std::size_t>(n));
            if (request_.size() > 65536) {
                break;
            }
        }
        if (headerEquals(request_, "content-length", "0")) {
            const char response[] =
                "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
            ::send(client, response, sizeof(response) - 1, MSG_NOSIGNAL);
        } else if (headerEquals(request_, "expect", "100-continue")) {
            // Same shape Jellyfin's host produces: acknowledge the expectation,
            // then wait for a body that never arrives while libcurl is blocked
            // on stdin. Replying 200 here would hide the hang.
            const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
            ::send(client, cont, sizeof(cont) - 1, MSG_NOSIGNAL);
            while (request_.size() < 65536) {
                const ssize_t n = ::recv(client, buf, sizeof(buf), 0);
                if (n <= 0) {
                    break;
                }
                request_.append(buf, static_cast<std::size_t>(n));
            }
        }
        ::close(client);
    }

    int listenFd_{-1};
    int port_{0};
    std::string request_;
    std::thread thread_;
};

/// Holds stdin open with no writer and no EOF, and clears a sticky EOF left by
/// a /dev/null redirect. Restores the previous stdin on destruction.
class BlockingStdin {
public:
    BlockingStdin() {
        saved_ = ::dup(STDIN_FILENO);
        REQUIRE(saved_ >= 0);
        int pipefd[2] = {-1, -1};
        REQUIRE(::pipe(pipefd) == 0);
        REQUIRE(::dup2(pipefd[0], STDIN_FILENO) == STDIN_FILENO);
        ::close(pipefd[0]);
        writeEnd_ = pipefd[1];
        ::clearerr(stdin);
    }

    ~BlockingStdin() {
        if (saved_ >= 0) {
            ::dup2(saved_, STDIN_FILENO);
            ::close(saved_);
            ::clearerr(stdin);
        }
        if (writeEnd_ >= 0) {
            ::close(writeEnd_);
        }
    }

private:
    int saved_{-1};
    int writeEnd_{-1};
};

#endif

} // namespace

TEST_CASE("empty POST completes without reading stdin") {
#ifndef _WIN32
    OneShotServer server;
    const BlockingStdin stdinHeldOpen;

    HttpRequest request;
    request.method = "POST";
    request.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/QuickConnect/Initiate";
    request.connectTimeout = std::chrono::seconds(2);
    request.totalTimeout = std::chrono::seconds(3);

    std::atomic<bool> done{false};
    koalaamp::servers::jellyfin::HttpResponse response;
    CurlTransport transport;
    std::thread worker([&] {
        koalaamp::servers::jellyfin::NoCancellation none;
        response = transport.perform(request, none);
        done.store(true);
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!done.load()) {
        // The worker is blocked in libcurl's stdin read. Closing the pipe would
        // hand it an EOF and hide the bug, and joining would hang the suite.
        std::fprintf(stderr, "empty POST blocked while stdin was still open\n");
        std::_Exit(1);
    }
    worker.join();
    server.join();

    if (!response.transportOk() || response.status != 200) {
        std::fprintf(stderr, "transport error='%s' status=%ld body='%s'\n--- request ---\n%s\n--- end ---\n",
                     response.error.c_str(), response.status, response.body.c_str(),
                     server.request().c_str());
    }
    REQUIRE(response.transportOk());
    CHECK(response.status == 200);
    CHECK(response.body == "ok");
    CHECK(server.request().rfind("POST /QuickConnect/Initiate ", 0) == 0);
    CHECK(headerEquals(server.request(), "content-length", "0"));
    CHECK_FALSE(headerEquals(server.request(), "transfer-encoding", "chunked"));
    CHECK_FALSE(headerEquals(server.request(), "expect", "100-continue"));
#else
    SKIP("stdin-backed POST is exercised on POSIX");
#endif
}
