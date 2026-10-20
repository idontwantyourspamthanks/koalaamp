// SPDX-License-Identifier: MIT
// servers — Jellyfin token in Windows Credential Manager (CRED_TYPE_GENERIC).

#include "servers/SecretStore.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>

#include <string>

namespace koalaamp::servers {
namespace {

std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int bytes = static_cast<int>(text.size());
    const int chars = MultiByteToWideChar(CP_UTF8, 0, text.data(), bytes, nullptr, 0);
    std::wstring out(chars > 0 ? static_cast<std::size_t>(chars) : 0, L'\0');
    if (chars > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), bytes, out.data(), chars);
    }
    return out;
}

std::wstring targetName(std::string_view key) {
    return L"KoalaAmp/" + widen(key);
}

class CredentialStore final : public SecretStore {
public:
    bool store(std::string_view key, std::string_view secret) override {
        const std::wstring target = targetName(key);
        CREDENTIALW cred{};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = const_cast<wchar_t*>(target.c_str());
        cred.CredentialBlobSize = static_cast<DWORD>(secret.size());
        cred.CredentialBlob =
            secret.empty() ? nullptr
                           : reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
        cred.UserName = const_cast<wchar_t*>(L"KoalaAmp");
        return CredWriteW(&cred, 0) == TRUE;
    }

    std::optional<std::string> load(std::string_view key) const override {
        const std::wstring target = targetName(key);
        PCREDENTIALW cred = nullptr;
        if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) != TRUE || cred == nullptr) {
            return std::nullopt;
        }
        std::string value;
        if (cred->CredentialBlob != nullptr && cred->CredentialBlobSize > 0) {
            value.assign(reinterpret_cast<const char*>(cred->CredentialBlob),
                         cred->CredentialBlobSize);
        }
        CredFree(cred);
        if (value.empty()) {
            return std::nullopt;
        }
        return value;
    }

    bool remove(std::string_view key) override {
        const std::wstring target = targetName(key);
        if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) == TRUE) {
            return true;
        }
        return GetLastError() == ERROR_NOT_FOUND;
    }
};

} // namespace

std::unique_ptr<SecretStore> makePlatformSecretStore() {
    return std::make_unique<CredentialStore>();
}

} // namespace koalaamp::servers
