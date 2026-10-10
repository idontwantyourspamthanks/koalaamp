// SPDX-License-Identifier: MIT
// jellyfin — MediaBrowser Authorization header construction. See AuthHeaders.hpp.

#include "servers/jellyfin/AuthHeaders.hpp"

#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace koalaamp::servers::jellyfin {

std::string defaultDeviceName() {
    char buf[256] = {};
#if defined(_WIN32)
    DWORD n = sizeof(buf);
    if (GetComputerNameA(buf, &n) && n > 0) {
        return std::string(buf, n);
    }
#else
    if (::gethostname(buf, sizeof(buf) - 1) == 0 && buf[0] != '\0') {
        return std::string(buf);
    }
#endif
    return "KoalaAmp";
}

std::string buildAuthHeader(const DeviceProfile& profile, std::string_view deviceId,
                            std::string_view token) {
    std::string out;
    out.reserve(128);
    out += "MediaBrowser ";
    out += "Client=\"" + profile.clientName + "\", ";
    out += "Device=\"" + profile.deviceName + "\", ";
    out += "DeviceId=\"" + std::string(deviceId) + "\", ";
    out += "Version=\"" + profile.clientVersion + "\"";
    if (!token.empty()) {
        out += ", Token=\"" + std::string(token) + "\"";
    }
    return out;
}

std::vector<std::pair<std::string, std::string>>
baseHeaders(const DeviceProfile& profile, std::string_view deviceId, std::string_view token) {
    return {
        {"Authorization", buildAuthHeader(profile, deviceId, token)},
        {"Accept", "application/json"},
    };
}

} // namespace koalaamp::servers::jellyfin
