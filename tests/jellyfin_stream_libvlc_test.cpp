// SPDX-License-Identifier: MIT
// SV-2 — direct play end-to-end: libvlc reads a Jellyfin item through
// `libvlc_media_new_callbacks`, no temp file and no URL credentials.
//
// The transport serves a generated WAV in memory; the request assertions pin the
// static endpoint and the header-only token. Frames > 0 means libvlc's demuxer
// genuinely drove open/read/seek on JellyfinMediaSource and decoded audio.

#include "interop/vlc/VlcPcmTap.hpp"
#include "servers/jellyfin/JellyfinMediaSource.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using koalaamp::servers::jellyfin::DeviceProfile;
using koalaamp::servers::jellyfin::HttpRequest;
using koalaamp::servers::jellyfin::HttpResponse;
using koalaamp::servers::jellyfin::IHttpTransport;
using koalaamp::servers::jellyfin::StreamSpec;

const std::string kBaseUrl = "http://jellyfin.local:8096";
const std::string kItemId = "77777777777777777777777777777777";

DeviceProfile testProfile() {
    DeviceProfile profile;
    profile.clientName = "KoalaAmp";
    profile.deviceName = "device-under-test";
    profile.clientVersion = "0.0.1";
    return profile;
}

/// 16-bit PCM WAV in memory. One request per `open`: the whole body, with the
/// real Content-Length, like Jellyfin's static stream answer.
class WavTransport final : public IHttpTransport {
public:
    explicit WavTransport(std::string bytes) : bytes_(std::move(bytes)) {}

    HttpResponse perform(const HttpRequest& request, const koalaamp::library::Cancellation&) override {
        requests.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mutex);
        lastUrl = request.url;
        lastAuthorization.clear();
        for (const auto& [key, value] : request.headers) {
            if (key == "Authorization") {
                lastAuthorization = value;
            }
        }
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
    std::string lastAuthorization;

private:
    std::string bytes_;
};

std::string makeSineWav(double hz, unsigned rate, unsigned channels, double seconds) {
    constexpr double kPi = 3.14159265358979323846;
    const auto frames = static_cast<std::uint32_t>(rate * seconds);
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
    u32(16);              // PCM chunk size
    u16(1);               // PCM
    u16(static_cast<std::uint16_t>(channels));
    u32(rate);
    u32(rate * channels * 2); // byte rate
    u16(static_cast<std::uint16_t>(channels * 2));
    u16(16);              // bits per sample
    out += "data";
    u32(dataBytes);
    for (std::uint32_t f = 0; f < frames; ++f) {
        const auto sample = static_cast<std::int16_t>(
            std::sin(2.0 * kPi * hz * static_cast<double>(f) / rate) * 12000.0);
        for (unsigned c = 0; c < channels; ++c) {
            u16(static_cast<std::uint16_t>(sample));
        }
    }
    return out;
}

} // namespace

TEST_CASE("libvlc decodes a Jellyfin item through the callback stream") {
    WavTransport transport{makeSineWav(440.0, 48000, 2, 0.5)};
    auto source = std::make_shared<koalaamp::servers::jellyfin::JellyfinMediaSource>(
        transport, testProfile(), kBaseUrl, "fixture-token-1", "device-under-test",
        StreamSpec{kItemId});

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
    CHECK(transport.lastUrl == kBaseUrl + "/Audio/" + kItemId + "/stream?static=true");
    CHECK(transport.lastUrl.find("api_key") == std::string::npos);
    CHECK(transport.lastAuthorization.find("Token=\"fixture-token-1\"") != std::string::npos);
}
