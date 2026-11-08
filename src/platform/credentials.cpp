#include "platform/credentials.h"

#include <cctype>
#include <cstdint>

#include "core/log.h"

#if defined(_WIN32)
#include <windows.h>
#include <wincred.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#else
#include <dlfcn.h>
#endif

namespace hailstone::platform {

std::string credential_server(std::string_view host, int port) {
    std::string out;
    for (char c : host) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out + ":" + std::to_string(port);
}

namespace {

#if defined(_WIN32)

// The Credential Manager's generic credentials, "Hailstone/<server>/<username>" (the A functions take UTF-8: the
// programs run with it as their code page, cmake/utf8.manifest).
std::string target_name(std::string_view server, std::string_view username) {
    return "Hailstone/" + std::string(server) + "/" + std::string(username);
}

std::string last_error(const char* what) { return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")"; }

class SystemCredentials : public CredentialStore {
public:
    bool available() const override { return true; }
    std::optional<std::string> load(std::string_view server, std::string_view username) override {
        PCREDENTIALA cred = nullptr;
        std::string target = target_name(server, username);
        if (!CredReadA(target.c_str(), CRED_TYPE_GENERIC, 0, &cred)) {
            if (GetLastError() != ERROR_NOT_FOUND) error_ = last_error("CredRead");
            return std::nullopt;
        }
        std::string password(reinterpret_cast<const char*>(cred->CredentialBlob), cred->CredentialBlobSize);
        CredFree(cred);
        return password;
    }
    bool save(std::string_view server, std::string_view username, std::string_view password) override {
        std::string target = target_name(server, username);
        std::string user(username);
        CREDENTIALA cred{};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = target.data();
        cred.UserName = user.data();
        cred.CredentialBlobSize = static_cast<DWORD>(password.size());
        cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(password.data()));
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
        if (CredWriteA(&cred, 0)) return true;
        error_ = last_error("CredWrite");
        return false;
    }
    bool erase(std::string_view server, std::string_view username) override {
        std::string target = target_name(server, username);
        if (CredDeleteA(target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND) return true;
        error_ = last_error("CredDelete");
        return false;
    }
};

#elif defined(__APPLE__)

// The login keychain's generic passwords: service "Hailstone <server>", account the username.
std::string service_name(std::string_view server) { return "Hailstone " + std::string(server); }

std::string status_text(const char* what, OSStatus status) {
    return std::string(what) + " failed (OSStatus " + std::to_string(status) + ")";
}

class SystemCredentials : public CredentialStore {
public:
    bool available() const override { return true; }
    std::optional<std::string> load(std::string_view server, std::string_view username) override {
        std::string service = service_name(server);
        UInt32 size = 0;
        void* data = nullptr;
        OSStatus status = SecKeychainFindGenericPassword(nullptr, static_cast<UInt32>(service.size()), service.data(),
                                                         static_cast<UInt32>(username.size()), username.data(), &size,
                                                         &data, nullptr);
        if (status != errSecSuccess) {
            if (status != errSecItemNotFound) error_ = status_text("SecKeychainFindGenericPassword", status);
            return std::nullopt;
        }
        std::string password(static_cast<const char*>(data), size);
        SecKeychainItemFreeContent(nullptr, data);
        return password;
    }
    bool save(std::string_view server, std::string_view username, std::string_view password) override {
        std::string service = service_name(server);
        SecKeychainItemRef item = nullptr;
        OSStatus status = SecKeychainFindGenericPassword(nullptr, static_cast<UInt32>(service.size()), service.data(),
                                                         static_cast<UInt32>(username.size()), username.data(),
                                                         nullptr, nullptr, &item);
        if (status == errSecSuccess) {
            status = SecKeychainItemModifyAttributesAndData(item, nullptr, static_cast<UInt32>(password.size()),
                                                            password.data());
            CFRelease(item);
        } else {
            status = SecKeychainAddGenericPassword(nullptr, static_cast<UInt32>(service.size()), service.data(),
                                                   static_cast<UInt32>(username.size()), username.data(),
                                                   static_cast<UInt32>(password.size()), password.data(), nullptr);
        }
        if (status == errSecSuccess) return true;
        error_ = status_text("saving to the keychain", status);
        return false;
    }
    bool erase(std::string_view server, std::string_view username) override {
        std::string service = service_name(server);
        SecKeychainItemRef item = nullptr;
        OSStatus status = SecKeychainFindGenericPassword(nullptr, static_cast<UInt32>(service.size()), service.data(),
                                                         static_cast<UInt32>(username.size()), username.data(),
                                                         nullptr, nullptr, &item);
        if (status == errSecItemNotFound) return true;
        if (status == errSecSuccess) {
            status = SecKeychainItemDelete(item);
            CFRelease(item);
        }
        if (status == errSecSuccess) return true;
        error_ = status_text("removing from the keychain", status);
        return false;
    }
};

#else

// libsecret's C API, as far as it is used here (libsecret/secret-schema.h, secret-password.h; glib's GError).
// The library is opened when first needed: a system without it has no saved passwords, and the build needs no
// headers for it.
struct SecretSchemaAttribute {
    const char* name;
    int type; // SECRET_SCHEMA_ATTRIBUTE_STRING = 0
};
struct SecretSchema {
    const char* name;
    int flags; // SECRET_SCHEMA_NONE = 0
    SecretSchemaAttribute attributes[32];
    int reserved;
    void* reserved1;
    void* reserved2;
    void* reserved3;
    void* reserved4;
    void* reserved5;
    void* reserved6;
    void* reserved7;
};
struct GError {
    std::uint32_t domain;
    int code;
    char* message;
};
using LookupFn = char* (*)(const SecretSchema*, void*, GError**, ...);
using StoreFn = int (*)(const SecretSchema*, const char*, const char*, const char*, void*, GError**, ...);
using ClearFn = int (*)(const SecretSchema*, void*, GError**, ...);
using FreeFn = void (*)(char*);
using ErrorFreeFn = void (*)(GError*);

const SecretSchema kSchema = {"org.hailstone.Password", 0, {{"server", 0}, {"username", 0}, {nullptr, 0}}, 0,
                              nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

class SystemCredentials : public CredentialStore {
public:
    SystemCredentials() {
        lib_ = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!lib_) {
            log::info("saved passwords: no libsecret-1.so.0 ({}), so none are kept", dlerror());
            return;
        }
        // dlsym on the handle looks in its dependencies too: glib's g_error_free.
        lookup_ = reinterpret_cast<LookupFn>(dlsym(lib_, "secret_password_lookup_sync"));
        store_ = reinterpret_cast<StoreFn>(dlsym(lib_, "secret_password_store_sync"));
        clear_ = reinterpret_cast<ClearFn>(dlsym(lib_, "secret_password_clear_sync"));
        free_ = reinterpret_cast<FreeFn>(dlsym(lib_, "secret_password_free"));
        error_free_ = reinterpret_cast<ErrorFreeFn>(dlsym(lib_, "g_error_free"));
        if (!lookup_ || !store_ || !clear_ || !free_ || !error_free_) {
            log::warn("saved passwords: libsecret-1.so.0 lacks the functions used, so none are kept");
            lookup_ = nullptr;
        }
    }
    ~SystemCredentials() override {
        if (lib_) dlclose(lib_);
    }
    bool available() const override { return lookup_ != nullptr; }
    std::optional<std::string> load(std::string_view server, std::string_view username) override {
        if (!available()) return std::nullopt;
        std::string s(server), u(username);
        GError* err = nullptr;
        char* found = lookup_(&kSchema, nullptr, &err, "server", s.c_str(), "username", u.c_str(), nullptr);
        if (failed(err, "reading the keyring")) return std::nullopt;
        if (!found) return std::nullopt;
        std::string password(found);
        free_(found); // clears it too
        return password;
    }
    bool save(std::string_view server, std::string_view username, std::string_view password) override {
        if (!available()) {
            error_ = "no keyring (libsecret) on this system";
            return false;
        }
        std::string s(server), u(username), p(password);
        std::string label = "Hailstone: " + u + " on " + s;
        GError* err = nullptr;
        int ok = store_(&kSchema, "default", label.c_str(), p.c_str(), nullptr, &err, "server", s.c_str(), "username",
                        u.c_str(), nullptr);
        return !failed(err, "saving to the keyring") && ok;
    }
    bool erase(std::string_view server, std::string_view username) override {
        if (!available()) return true;
        std::string s(server), u(username);
        GError* err = nullptr;
        clear_(&kSchema, nullptr, &err, "server", s.c_str(), "username", u.c_str(), nullptr);
        return !failed(err, "removing from the keyring");
    }

private:
    bool failed(GError* err, const char* what) {
        if (!err) return false;
        error_ = std::string(what) + ": " + (err->message ? err->message : "unknown error");
        log::warn("saved passwords: {}", error_);
        error_free_(err);
        return true;
    }

    void* lib_ = nullptr;
    LookupFn lookup_ = nullptr;
    StoreFn store_ = nullptr;
    ClearFn clear_ = nullptr;
    FreeFn free_ = nullptr;
    ErrorFreeFn error_free_ = nullptr;
};

#endif

} // namespace

CredentialStore& system_credentials() {
    static SystemCredentials store;
    return store;
}

} // namespace hailstone::platform
