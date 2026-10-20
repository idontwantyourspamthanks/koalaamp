// SPDX-License-Identifier: MIT
// SV-4 — libvlc decodes a PMS part through the callback media source.

#include "interop/vlc/VlcPcmTap.hpp"
#include "servers/plex/PlexMediaSource.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {

using koalaamp::servers::jellyfin::HttpRequest;
using koalaamp::servers::jellyfin::HttpResponse;
using koalaamp::servers::jellyfin::IHttpTransport;
using koalaamp::servers::plex::PlexClientInfo;

PlexClientInfo clientInfo() {
    return PlexClientInfo{"client-id", "KoalaAmp", "0.0.1", "tester", "Generic"};
}

class WavTransport final : public IHttpTransport {
public:
    explicit WavTransport(std::string bytes) : bytes_(std::move(bytes)) {}

    HttpResponse perform(const HttpRequest& request,
                         const koalaamp::library::Cancellation&) override {
        std::lock_guard<std::mutex> lock(mutex);
        lastUrl = request.url;
        requests.fetch_add(1);
        HttpResponse response;
        response.status = 200;
        response.body = bytes_;
        response.headers.emplace_back("Content-Type", "audio/wav");
        response.headers.emplace_back("Content-Length", std::to_string(bytes_.size()));
        return response;
    }

    std::atomic<int> requests{0};
    std::mutex mutex;
    std::string lastUrl;

private:
    std::string bytes_;
};

std::string makeSineWav() {
    constexpr double kPi = 3.14159265358979323846;
    const unsigned rate = 48000;
    const unsigned channels = 2;
    const auto frames = static_cast<std::uint32_t>(rate / 2);
    const auto dataBytes = frames * channels * 2;
    std::string out;
    auto u16 = [&](std::uint16_t v) {
        out.push_back(static_cast<char>(v & 0xff));
        out.push_back(static_cast<char>((v >> 8) & 0xff));
    };
    auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
        }
    };
    out += "RIFF";
    u32(36 + dataBytes);
    out += "WAVE";
    out += "fmt ";
    u32(16);
    u16(1);
    u16(static_cast<std::uint16_t>(channels));
    u32(rate);
    u32(rate * channels * 2);
    u16(static_cast<std::uint16_t>(channels * 2));
    u16(16);
    out += "data";
    u32(dataBytes);
    for (std::uint32_t f = 0; f < frames; ++f) {
        const auto sample = static_cast<std::int16_t>(
            std::sin(2.0 * kPi * 440.0 * static_cast<double>(f) / rate) * 12000.0);
        for (unsigned c = 0; c < channels; ++c) {
            u16(static_cast<std::uint16_t>(sample));
        }
    }
    return out;
}

} // namespace

TEST_CASE("libvlc decodes a Plex part through the callback stream") {
    WavTransport transport{makeSineWav()};
    auto source = std::make_shared<koalaamp::servers::plex::PlexMediaSource>(
        transport, clientInfo(), "http://plex.local:32400", "pms-token",
        "/library/parts/1/file.wav");

    koalaamp::interop::vlc::VlcPcmTap tap;
    std::atomic<std::uint64_t> frames{0};
    tap.setPcmObserver([&](const float*, std::size_t count, unsigned, unsigned) {
        frames.fetch_add(count, std::memory_order_relaxed);
    });
    REQUIRE(tap.setMediaStream(source));
    tap.play();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (frames.load(std::memory_order_relaxed) == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    tap.stop();

    CHECK(transport.requests.load() >= 1);
    CHECK(frames.load() >= 1);
    std::lock_guard<std::mutex> lock(transport.mutex);
    CHECK(transport.lastUrl == "http://plex.local:32400/library/parts/1/file.wav");
}
