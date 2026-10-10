// SPDX-License-Identifier: MIT
// jellyfin — client device identity. See DeviceIdentity.hpp.

#include "servers/jellyfin/DeviceIdentity.hpp"

#include "servers/jellyfin/Sha256.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <system_error>

namespace koalaamp::servers::jellyfin {

std::string deriveDeviceId(std::string_view machineId, std::string_view userKey) {
    std::string seed;
    seed.reserve(machineId.size() + 1 + userKey.size());
    seed.append(machineId);
    seed.push_back(':');
    seed.append(userKey);
    return sha256Hex(seed);
}

std::string randomUuid() {
    std::array<std::uint8_t, 16> bytes{};
    std::random_device rd;
    for (auto& b : bytes) {
        b = static_cast<std::uint8_t>(rd() & 0xffu);
    }
    // RFC 4122: version 4 + variant 10xx.
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fu) | 0x40u);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fu) | 0x80u);

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out.push_back('-');
        }
        out.push_back(kHex[bytes[i] >> 4]);
        out.push_back(kHex[bytes[i] & 0x0fu]);
    }
    return out;
}

namespace {

[[maybe_unused]] std::string envOr(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : std::string(fallback);
}

std::string homeDir() {
    const char* h = std::getenv("HOME");
    return h != nullptr ? std::string(h) : std::string(".");
}

} // namespace

std::string defaultMachineIdPath() {
#if defined(_WIN32)
    return envOr("LOCALAPPDATA", ".") + "/KoalaAmp/machine-id";
#elif defined(__APPLE__)
    return homeDir() + "/Library/Application Support/KoalaAmp/machine-id";
#else
    const char* state = std::getenv("XDG_STATE_HOME");
    const std::string base =
        (state != nullptr && *state != '\0') ? std::string(state) : homeDir() + "/.local/state";
    return base + "/koalaamp/machine-id";
#endif
}

MachineIdStore::MachineIdStore(std::string path)
    : path_(path.empty() ? defaultMachineIdPath() : std::move(path)) {}

std::string MachineIdStore::machineId() {
    if (!cached_.empty()) {
        return cached_;
    }

    std::ifstream in(path_, std::ios::binary);
    if (in) {
        std::string id;
        std::getline(in, id);
        // Trim trailing CR/whitespace so a text-mode-written file round-trips.
        while (!id.empty() && (id.back() == '\r' || id.back() == '\n' || id.back() == ' ')) {
            id.pop_back();
        }
        if (!id.empty()) {
            cached_ = id;
            return cached_;
        }
    }

    cached_ = randomUuid();
    // Best-effort persist; a read-only home must not break sign-in.
    std::error_code ec;
    const auto slash = path_.find_last_of("/\\");
    if (slash != std::string::npos) {
        std::filesystem::create_directories(path_.substr(0, slash), ec);
    }
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (out) {
        out << cached_ << '\n';
    }
    return cached_;
}

} // namespace koalaamp::servers::jellyfin
