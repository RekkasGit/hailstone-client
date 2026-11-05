#pragma once

// Saved passwords, kept in the system's own keyring and never in the data folder: the Secret Service (GNOME
// Keyring, KWallet ...) through libsecret, loaded when first used so the client still runs without it; the
// Credential Manager on Windows; the login keychain on macOS. Each password is keyed by the login server's address
// (credential_server()) and the account's username, so two saved servers with one address share it.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace nmsc::platform {

class CredentialStore {
public:
    virtual ~CredentialStore() = default;
    // False when this system has no keyring Hailstone can use (no libsecret, say): nothing is kept then.
    virtual bool available() const = 0;
    // The saved password, nullopt when there is none or the keyring refused (error() says why).
    virtual std::optional<std::string> load(std::string_view server, std::string_view username) = 0;
    // Replaces any password saved before; false when the keyring refused.
    virtual bool save(std::string_view server, std::string_view username, std::string_view password) = 0;
    // True when it is gone (or was never there).
    virtual bool erase(std::string_view server, std::string_view username) = 0;
    // What the last call that failed was told, for the status line.
    const std::string& error() const { return error_; }

protected:
    std::string error_;
};

// The system's keyring (one for the process).
CredentialStore& system_credentials();

// "host:port" with the host in lower case: the key a password is saved under.
std::string credential_server(std::string_view host, int port);

// Kept in memory only: the tests', and a stand-in where a keyring must not be touched.
class MemoryCredentials : public CredentialStore {
public:
    bool available() const override { return true; }
    std::optional<std::string> load(std::string_view server, std::string_view username) override {
        auto it = saved_.find({std::string(server), std::string(username)});
        if (it == saved_.end()) return std::nullopt;
        return it->second;
    }
    bool save(std::string_view server, std::string_view username, std::string_view password) override {
        saved_[{std::string(server), std::string(username)}] = std::string(password);
        return true;
    }
    bool erase(std::string_view server, std::string_view username) override {
        saved_.erase({std::string(server), std::string(username)});
        return true;
    }
    std::size_t size() const { return saved_.size(); }

private:
    std::map<std::pair<std::string, std::string>, std::string> saved_;
};

} // namespace nmsc::platform
