#pragma once

// Shared by the add-on tests (docs/addons.md): packages made in memory, a library header for this build's platform,
// a signing key, and a fake GitHub that serves files from memory and records what was asked for.

#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "app/addon_manager.h"
#include "core/addon_source.h"
#include "core/binary_header.h"
#include "core/ed25519.h"
#include "core/sha256.h"
#include "core/update_manifest.h"
#include "core/zip_write.h"

namespace hailstone::addon_test {

inline std::string zip_of(const std::map<std::string, std::string>& files) {
    ZipWriter z;
    std::tm when{};
    when.tm_year = 120;
    when.tm_mday = 1;
    for (const auto& [name, data] : files) z.add(name, data, when);
    const auto& bytes = z.finish();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// The first bytes of a shared library this platform loads (header only: it is never loaded).
inline std::string library_for(std::string_view platform) {
    std::string b(512, '\0');
    auto put16 = [&](std::size_t at, unsigned v) {
        b[at] = static_cast<char>(v & 0xff);
        b[at + 1] = static_cast<char>((v >> 8) & 0xff);
    };
    auto put32 = [&](std::size_t at, unsigned v) {
        for (std::size_t i = 0; i < 4; ++i) b[at + i] = static_cast<char>((v >> (8 * i)) & 0xff);
    };
    const bool arm = platform.ends_with("arm64");
    if (platform.starts_with("linux")) {
        b[0] = 0x7f, b[1] = 'E', b[2] = 'L', b[3] = 'F', b[4] = 2, b[5] = 1, b[6] = 1;
        put16(16, 3);
        put16(18, arm ? 183 : 62);
    } else if (platform.starts_with("windows")) {
        b[0] = 'M', b[1] = 'Z';
        put32(0x3c, 0x80);
        put32(0x80, 0x00004550);
        put16(0x84, arm ? 0xaa64 : 0x8664);
        put16(0x84 + 18, 0x2022);
    } else {
        put32(0, 0xfeedfacf);
        put32(4, arm ? 0x0100000cu : 0x01000007u);
        put32(12, 6);
    }
    return b;
}

// The first bytes of a program this platform runs (header only: it is never run): ELF ET_EXEC, a PE executable image
// that is not a DLL, a Mach-O MH_EXECUTE.
inline std::string program_for(std::string_view platform) {
    std::string b = library_for(platform);
    if (platform.starts_with("linux")) {
        b[16] = 2; // ET_EXEC
    } else if (platform.starts_with("windows")) {
        b[0x84 + 18] = 0x22; // IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE, no IMAGE_FILE_DLL
        b[0x84 + 19] = 0x00;
    } else {
        b[12] = 2; // MH_EXECUTE
    }
    return b;
}

inline std::string this_platform() { return std::string(update_platform()); }
inline std::string lib_ext() { return std::string(plugin_extension(update_platform())); }

struct Keys {
    Ed25519SecretKey secret{};
    Ed25519PublicKey pub{};
    explicit Keys(std::uint8_t seed_byte = 5) {
        std::array<std::uint8_t, 32> seed{};
        seed.fill(seed_byte);
        ed25519_key_pair(seed, secret, pub);
    }
    std::string sign(std::string body) const {
        body += "key " + hex_encode(pub) + "\n";
        return body + "signature " + hex_encode(ed25519_sign(secret, body)) + "\n";
    }
};

// A manifest for a package; `platform` "any" for a script.
inline std::string manifest_for(const std::string& name, const std::string& kind, const std::string& version,
                                const std::string& caps, const std::string& platform, const std::string& zip,
                                const std::string& file, const std::string& extra = {}) {
    return "hailstone-addon 1\nname " + name + "\nkind " + kind + "\nversion " + version + "\ncaps " + caps +
           "\ndescription A test add-on.\npackage " + platform + " " + std::to_string(zip.size()) + " " +
           to_hex(sha256(zip)) + " " + file + "\n" + extra;
}

// GitHub in memory: address -> body. Records every address asked, and refuses what the manager's allow_url
// refuses (as platform::http_get does with the address a transfer ends on).
struct FakeGitHub {
    std::map<std::string, std::string> files;
    std::map<std::string, std::string> redirects; // address -> where it "redirects"
    std::vector<std::string> asked;
    std::mutex mutex;

    void release(const std::string& repo, const std::string& manifest, const std::string& zip_name,
                 const std::string& zip) {
        std::lock_guard lock(mutex);
        files["https://github.com/" + repo + "/releases/latest/download/hailstone-addon.txt"] = manifest;
        files["https://github.com/" + repo + "/releases/latest/download/" + zip_name] = zip;
    }

    app::AddonFetch fetch() {
        return [this](const std::string& url, const std::function<bool(std::string_view)>& allow,
                      const std::function<bool(std::span<const std::byte>)>& sink, const std::function<bool()>&) {
            platform::HttpGetResult r;
            std::string body;
            std::string at = url;
            {
                std::lock_guard lock(mutex);
                asked.push_back(url);
                if (auto red = redirects.find(url); red != redirects.end()) at = red->second;
                auto it = files.find(at);
                if (it == files.end()) {
                    r.status = 404;
                    r.error = "the server answered HTTP 404";
                    return r;
                }
                body = it->second;
            }
            r.status = 200;
            if (allow && !allow(at)) {
                r.error = "the download was sent to an address it may not come from";
                return r;
            }
            if (!sink(std::as_bytes(std::span(body.data(), body.size())))) {
                r.error = "cancelled";
                return r;
            }
            r.ok = true;
            return r;
        };
    }
};

inline void put_file(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace hailstone::addon_test
