// SPDX-License-Identifier: MIT
// servers — Jellyfin token in the macOS login keychain (generic password).

#include "servers/SecretStore.hpp"

#include <Security/Security.h>

#include <string>

namespace koalaamp::servers {
namespace {

template <typename T>
struct CfRef {
    T ref = nullptr;
    ~CfRef() {
        if (ref != nullptr) {
            CFRelease(ref);
        }
    }
    CfRef() = default;
    explicit CfRef(T value) : ref(value) {}
    CfRef(const CfRef&) = delete;
    CfRef& operator=(const CfRef&) = delete;
    CfRef(CfRef&& other) noexcept : ref(other.ref) { other.ref = nullptr; }
    CfRef& operator=(CfRef&& other) noexcept {
        if (this != &other) {
            if (ref != nullptr) {
                CFRelease(ref);
            }
            ref = other.ref;
            other.ref = nullptr;
        }
        return *this;
    }
    operator T() const { return ref; }
};

CfRef<CFStringRef> cfString(std::string_view text) {
    return CfRef<CFStringRef>(CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),
        static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false));
}

CfRef<CFMutableDictionaryRef> baseQuery(std::string_view key) {
    CfRef<CFMutableDictionaryRef> query(CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    const auto service = cfString("KoalaAmp");
    const auto account = cfString(key);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    return query;
}

class KeychainStore final : public SecretStore {
public:
    bool store(std::string_view key, std::string_view secret) override {
        auto query = baseQuery(key);
        CfRef<CFDataRef> data(CFDataCreate(kCFAllocatorDefault,
                                           reinterpret_cast<const UInt8*>(secret.data()),
                                           static_cast<CFIndex>(secret.size())));
        CFMutableDictionaryRef update = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(update, kSecValueData, data);
        CFDictionarySetValue(update, kSecAttrAccessible,
                             kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);

        OSStatus status = SecItemUpdate(query, update);
        if (status == errSecItemNotFound) {
            CFDictionarySetValue(query, kSecValueData, data);
            CFDictionarySetValue(query, kSecAttrAccessible,
                                 kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
            status = SecItemAdd(query, nullptr);
        }
        CFRelease(update);
        return status == errSecSuccess;
    }

    std::optional<std::string> load(std::string_view key) const override {
        auto query = baseQuery(key);
        CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef found = nullptr;
        const OSStatus status = SecItemCopyMatching(query, &found);
        if (status != errSecSuccess || found == nullptr) {
            return std::nullopt;
        }
        CfRef<CFDataRef> data(static_cast<CFDataRef>(found));
        const auto* bytes = CFDataGetBytePtr(data);
        const CFIndex length = CFDataGetLength(data);
        if (bytes == nullptr || length <= 0) {
            return std::nullopt;
        }
        return std::string(reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(length));
    }

    bool remove(std::string_view key) override {
        auto query = baseQuery(key);
        const OSStatus status = SecItemDelete(query);
        return status == errSecSuccess || status == errSecItemNotFound;
    }
};

} // namespace

std::unique_ptr<SecretStore> makePlatformSecretStore() {
    return std::make_unique<KeychainStore>();
}

} // namespace koalaamp::servers
