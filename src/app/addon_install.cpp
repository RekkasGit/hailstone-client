#include "app/addon_install.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

#include <lua.hpp>

#include "app/update_install.h"
#include "core/atomic_file.h"
#include "core/binary_header.h"
#include "core/log.h"
#include "core/update_manifest.h"
#include "core/zip_read.h"
#include "net/server_files.h"
#include "plugin/native_plugins.h"
#include "plugin/policy.h"

namespace nmsc::app {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxFiles = 4096;
constexpr std::uint64_t kMaxScriptBytes = 64ull * 1024 * 1024;   // all of a script's files, unpacked
constexpr std::uint64_t kMaxPluginBytes = 512ull * 1024 * 1024;  // all of a plugin's files, unpacked
constexpr std::uint64_t kMaxLuaFile = 4ull * 1024 * 1024;        // one Lua file, read whole to compile it
constexpr std::uint64_t kMaxLibrary = 256ull * 1024 * 1024;      // the plugin's library, read whole for its header
constexpr std::string_view kRegistryHeader = "hailstone-addons 1";

fs::path rel(std::string_view name) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size()));
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool iequal(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

std::pair<std::string_view, std::string_view> split(std::string_view line) {
    const auto sp = line.find(' ');
    if (sp == std::string_view::npos) return {line, {}};
    return {line.substr(0, sp), line.substr(sp + 1)};
}

// A data-folder-relative path as the journal writes it.
std::string journal_path(const fs::path& data_dir, const fs::path& p) {
    const auto u8 = p.lexically_relative(data_dir).generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

class Journal {
public:
    explicit Journal(const fs::path& data_dir) : data_dir_(data_dir), out_(addons_dir(data_dir) / "journal.txt",
                                                                            std::ios::binary | std::ios::trunc) {}
    bool ok() const { return static_cast<bool>(out_); }
    // Moves `from` to `to` (making its folder), written to the journal first.
    bool move(const fs::path& from, const fs::path& to, std::string* error) {
        out_ << "move\t" << journal_path(data_dir_, from) << "\t" << journal_path(data_dir_, to) << "\n";
        out_.flush();
        if (!out_) {
            if (error) *error = "cannot write the add-ons' journal";
            return false;
        }
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        fs::rename(from, to, ec);
        if (ec && error) *error = "cannot move " + from.string() + " to " + to.string() + ": " + ec.message();
        return !ec;
    }
    void done() {
        out_ << "done\n";
        out_.flush();
    }
    void close() { out_.close(); }

private:
    fs::path data_dir_;
    std::ofstream out_;
};

void remove_quietly(const fs::path& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec) log::warn("Add-ons: cannot remove {}: {}", path.string(), ec.message());
}

// Undoes the journal's moves, last first.
void undo_journal(const fs::path& data_dir) {
    auto text = read_text_file(addons_dir(data_dir) / "journal.txt");
    if (!text) return;
    std::vector<std::pair<std::string, std::string>> moves;
    std::istringstream in(*text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto t1 = line.find('\t');
        const auto t2 = t1 == std::string::npos ? std::string::npos : line.find('\t', t1 + 1);
        if (line.substr(0, t1) != "move" || t2 == std::string::npos) continue;
        std::string from = line.substr(t1 + 1, t2 - t1 - 1), to = line.substr(t2 + 1);
        // Only paths inside the data folder: the journal is the client's own file, but a damaged one moves nothing
        // elsewhere.
        if (!is_safe_zip_path(from) || !is_safe_zip_path(to)) continue;
        moves.emplace_back(std::move(from), std::move(to));
    }
    std::reverse(moves.begin(), moves.end());
    for (const auto& [from, to] : moves) {
        std::error_code ec;
        if (!fs::exists(data_dir / rel(to), ec)) continue;
        fs::create_directories((data_dir / rel(from)).parent_path(), ec);
        fs::remove(data_dir / rel(from), ec); // a half-moved file in the way
        fs::rename(data_dir / rel(to), data_dir / rel(from), ec);
        if (ec) log::warn("Add-ons: undoing a cut-off install: cannot move {} back: {}", to, ec.message());
    }
}

bool journal_done(const fs::path& data_dir) {
    auto text = read_text_file(addons_dir(data_dir) / "journal.txt");
    return text && (text->ends_with("done\n") || text->ends_with("done\r\n"));
}

// Removes the empty folders under `root` (and `root` itself when `self`).
void remove_empty_dirs(const fs::path& root, bool self) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;
    std::vector<fs::path> dirs;
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec))
        if (it->is_directory(ec)) dirs.push_back(it->path());
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
        return a.native().size() > b.native().size();
    });
    for (const auto& d : dirs)
        if (fs::is_empty(d, ec)) fs::remove(d, ec);
    if (self && fs::is_empty(root, ec)) fs::remove(root, ec);
}

std::string file_of(std::string_view path) {
    const auto slash = path.rfind('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

std::string extension_of(std::string_view path) {
    const std::string name = lower(file_of(path));
    const auto dot = name.rfind('.');
    return dot == std::string::npos ? std::string() : name.substr(dot);
}

} // namespace

fs::path addons_dir(const fs::path& data_dir) { return data_dir / "addons"; }

fs::path addon_folder(const fs::path& data_dir, AddonKind kind, std::string_view name) {
    return data_dir / (kind == AddonKind::Script ? "scripts" : "plugins") / rel(name);
}

std::string_view addon_kind_word(AddonKind kind) { return kind == AddonKind::Script ? "script" : "plugin"; }

std::vector<InstalledAddon> read_installed_addons(const fs::path& data_dir) {
    std::vector<InstalledAddon> out;
    auto text = read_text_file(addons_dir(data_dir) / "installed.txt");
    if (!text) return out;
    std::istringstream in(*text);
    bool header = false;
    bool bad = false; // the current record has a line that does not read: it is dropped
    auto close = [&] {
        if (!out.empty() && bad) out.pop_back();
        bad = false;
    };
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (!header) {
            if (line != kRegistryHeader) {
                log::warn("Add-ons: addons/installed.txt is not one this version reads");
                return out;
            }
            header = true;
            continue;
        }
        auto [what, rest] = split(line);
        if (what == "addon") {
            close();
            auto [name, kind] = split(rest);
            InstalledAddon a;
            a.name = std::string(name);
            a.kind = kind == "plugin" ? AddonKind::Plugin : AddonKind::Script;
            bad = !addon_name_ok(name) || (kind != "plugin" && kind != "script");
            out.push_back(std::move(a));
            continue;
        }
        if (out.empty()) continue;
        InstalledAddon& a = out.back();
        if (what == "source") a.source = std::string(rest);
        else if (what == "version") a.version = std::string(rest);
        else if (what == "caps") a.caps = plugin::parse_caps(rest);
        else if (what == "key") {
            a.key = hex_decode<32>(rest);
            bad = bad || !a.key;
        } else if (what == "auto") a.auto_update = rest != "0";
        else if (what == "author") a.author = std::string(rest);
        else if (what == "description") a.description = std::string(rest);
        else if (what == "notes") a.notes = std::string(rest);
        else if (what == "file") {
            auto [hash, rest2] = split(rest);
            auto [size, path] = split(rest2);
            AddonFile f;
            auto digest = parse_sha256(hash);
            auto [end, ec] = std::from_chars(size.data(), size.data() + size.size(), f.size);
            if (!digest || ec != std::errc{} || end != size.data() + size.size() || !is_safe_zip_path(path)) {
                bad = true;
                continue;
            }
            f.sha256 = *digest;
            f.path = std::string(path);
            a.files.push_back(std::move(f));
        }
    }
    close();
    return out;
}

bool write_installed_addons(const fs::path& data_dir, const std::vector<InstalledAddon>& addons) {
    std::string text = "# The add-ons Hailstone installed (docs/addons.md). Written by the client.\n";
    text += kRegistryHeader;
    text += "\n";
    for (const auto& a : addons) {
        text += "\naddon " + a.name + " " + std::string(addon_kind_word(a.kind)) + "\n";
        text += "source " + a.source + "\n";
        text += "version " + a.version + "\n";
        text += "caps " + plugin::caps_text(a.caps) + "\n";
        if (a.key) text += "key " + hex_encode(*a.key) + "\n";
        text += std::string("auto ") + (a.auto_update ? "1" : "0") + "\n";
        if (!a.author.empty()) text += "author " + a.author + "\n";
        if (!a.description.empty()) text += "description " + a.description + "\n";
        if (!a.notes.empty()) text += "notes " + a.notes + "\n";
        for (const auto& f : a.files) text += "file " + to_hex(f.sha256) + " " + std::to_string(f.size) + " " + f.path + "\n";
    }
    return write_file_atomic(addons_dir(data_dir) / "installed.txt", text);
}

const InstalledAddon* find_addon(const std::vector<InstalledAddon>& addons, std::string_view name) {
    for (const auto& a : addons)
        if (iequal(a.name, name)) return &a;
    return nullptr;
}

std::vector<std::string> modified_addon_files(const fs::path& data_dir, const InstalledAddon& addon) {
    std::vector<std::string> out;
    const fs::path folder = addon_folder(data_dir, addon.kind, addon.name);
    for (const auto& f : addon.files)
        if (!package_file_matches(folder / rel(f.path), f.size, f.sha256)) out.push_back(f.path);
    return out;
}

namespace {
// Why `name` is taken in the data folder's own folder for `kind` ("" it is not): a folder there that is not empty
// (a plugin's that holds no library is only the settings a plugin left beside it), or a file of that name.
std::string taken_on_disk(const fs::path& data_dir, AddonKind kind, std::string_view name) {
    std::error_code ec;
    const fs::path folder = addon_folder(data_dir, kind, name);
    const std::string where = kind == AddonKind::Script ? "scripts/" : "plugins/";
    // A plugin's folder that holds no library is only the settings a plugin left beside it (nmsc->config_dir), as
    // after an update took out a plugin an older release shipped: the install keeps them, as an update does.
    bool holds_library = kind == AddonKind::Script;
    if (kind == AddonKind::Plugin && fs::is_directory(folder, ec)) {
        const std::string_view ext = plugin_extension(update_platform());
        for (const auto& e : fs::directory_iterator(folder, ec))
            if (e.is_regular_file(ec) && (ext.empty() || iequal(e.path().extension().string(), ext)))
                holds_library = true;
    }
    if (fs::exists(folder, ec) && !(fs::is_directory(folder, ec) && (fs::is_empty(folder, ec) || !holds_library)))
        return where + std::string(name) + "/ is there already (installed by hand, or with Hailstone)";
    // A script file of that name would be found before the add-on's folder, and a plugin file of that name is the
    // same plugin.
    const fs::path parent = folder.parent_path();
    for (const auto& e : fs::directory_iterator(parent, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string file = e.path().filename().string();
        if (kind == AddonKind::Script && iequal(file, std::string(name) + ".lua"))
            return where + file + " is there already (installed by hand)";
        if (kind == AddonKind::Plugin && iequal(plugin::plugin_name(e.path()), name) &&
            !plugin_extension(update_platform()).empty() && iequal(e.path().extension().string(), plugin_extension(update_platform())))
            return where + file + " is there already (installed by hand)";
    }
    return {};
}
} // namespace

std::string addon_name_taken(const fs::path& data_dir, const std::vector<InstalledAddon>& addons, AddonKind kind,
                             std::string_view name) {
    if (!addon_name_ok(name)) return "\"" + std::string(name) + "\" is not a name an add-on can have";
    if (kind == AddonKind::Script && iequal(name, "config"))
        return "\"config\" is the scripts' settings folder (scripts/config/)";
    if (const InstalledAddon* a = find_addon(addons, name))
        return "the add-on \"" + a->name + "\" is installed already";
    if (std::string why = taken_on_disk(data_dir, kind, name); !why.empty()) return why;
    // Nor a script and a compiled plugin of one name: their windows are told apart by kind
    // (plugin::UiBridge::owner_tag), but the player would not tell them apart.
    const AddonKind other = kind == AddonKind::Script ? AddonKind::Plugin : AddonKind::Script;
    if (std::string why = taken_on_disk(data_dir, other, name); !why.empty())
        return why + ": a script and a plugin may not share a name";
    return {};
}

bool addon_owns_folder(const fs::path& data_dir, std::string_view folder) {
    for (const auto& a : read_installed_addons(data_dir)) {
        const std::string mine = std::string(a.kind == AddonKind::Script ? "scripts/" : "plugins/") + a.name;
        if (iequal(mine, folder)) return true;
    }
    return false;
}

bool lua_compiles(std::string_view text, std::string_view chunk_name, std::string* error) {
    lua_State* L = luaL_newstate();
    if (!L) {
        if (error) *error = "out of memory";
        return false;
    }
    const std::string chunk = "=" + std::string(chunk_name);
    const bool ok = luaL_loadbufferx(L, text.data(), text.size(), chunk.c_str(), "t") == LUA_OK;
    if (!ok && error) {
        const char* msg = lua_tostring(L, -1);
        *error = msg ? msg : "does not compile";
    }
    lua_close(L);
    return ok;
}

std::optional<std::vector<AddonFile>> unpack_addon(const fs::path& data_dir, const AddonManifest& m,
                                                   const fs::path& zip_path, std::string_view platform,
                                                   std::string* error) {
    auto fail = [&](std::string why) -> std::optional<std::vector<AddonFile>> {
        if (error) *error = std::move(why);
        return std::nullopt;
    };
    ZipReader zip;
    if (!zip.open(zip_path)) return fail("the package is not a zip this reads: " + zip.error());
    const bool script = m.kind == AddonKind::Script;
    const std::string top = std::string(script ? "scripts/" : "plugins/") + m.name + "/";
    const std::string_view lib_ext = plugin_extension(platform);
    const std::uint64_t max_bytes = script ? kMaxScriptBytes : kMaxPluginBytes;

    // First the whole list, before anything is written.
    std::vector<const ZipEntry*> files;
    std::map<std::string, std::string> seen; // lower-case path -> as written: names only case apart are one file on
                                             // Windows and macOS
    std::uint64_t total = 0;
    const ZipEntry* library = nullptr;
    bool init = false;
    for (const auto& e : zip.entries()) {
        if (e.is_directory()) continue;
        if (files.size() >= kMaxFiles) return fail("the package holds too many files");
        total += e.size;
        if (total > max_bytes) return fail("the package is too large");
        const std::string full = top + e.name;
        bool other_platform = false;
        const auto dest = net::server_file_destination(full, &other_platform);
        if (!dest || *dest != full) {
            if (other_platform) return fail(e.name + " is for another platform");
            return fail(e.name + " is not something " + std::string(script ? "a script" : "a plugin") + " may hold" +
                        (script ? "" : " (Lua goes in ui/<pack>/, programs are refused)"));
        }
        if (!seen.emplace(lower(e.name), e.name).second) return fail(e.name + " is in the package twice");
        const std::string ext = extension_of(e.name);
        if (!script && !lib_ext.empty() && ext == lib_ext) {
            if (e.name.find('/') != std::string::npos)
                return fail(e.name + ": the plugin's library goes at the top of its package");
            if (library) return fail("the package holds more than one library");
            if (!iequal(plugin::plugin_name(rel(e.name)), m.name))
                return fail(e.name + ": the library is named after the plugin (" + m.name + std::string(lib_ext) + ")");
            library = &e;
        }
        if (script && iequal(e.name, "init.lua")) init = true;
        files.push_back(&e);
    }
    if (script && !init) return fail("a script's package has init.lua at its top");
    if (!script && !library) return fail("the package has no library for " + std::string(platform));

    // The checks that read a file's contents: each Lua file compiles, and the library is one this platform loads.
    for (const ZipEntry* e : files) {
        const bool lua = extension_of(e->name) == ".lua";
        if (!lua && e != library) continue;
        if (e->size > (lua ? kMaxLuaFile : kMaxLibrary)) return fail(e->name + " is too large");
        std::string bytes;
        bytes.reserve(e->size);
        if (!zip.extract(*e, [&](std::span<const std::byte> piece) {
                bytes.append(reinterpret_cast<const char*>(piece.data()), piece.size());
                return true;
            }))
            return fail(e->name + ": " + zip.error());
        if (lua) {
            std::string why;
            if (!lua_compiles(bytes, m.name + "/" + e->name, &why)) return fail(e->name + " does not compile: " + why);
        } else {
            const BinaryHeader h = read_binary_header(std::as_bytes(std::span(bytes.data(), bytes.size())));
            if (!binary_fits_platform(h, platform)) return fail(e->name + " is not a library for " + std::string(platform));
        }
    }

    // Then out into new/<name>/, each file hashed as it is written.
    const fs::path dir = addons_dir(data_dir) / "new" / rel(m.name);
    remove_quietly(dir);
    std::vector<AddonFile> out;
    for (const ZipEntry* e : files) {
        const fs::path to = dir / rel(e->name);
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        std::ofstream file(to, std::ios::binary | std::ios::trunc);
        if (!file) return fail("cannot write " + to.string());
        Sha256 h;
        AddonFile f;
        f.path = e->name;
        f.size = e->size;
        if (!zip.extract(*e, [&](std::span<const std::byte> piece) {
                h.update(piece);
                file.write(reinterpret_cast<const char*>(piece.data()), static_cast<std::streamsize>(piece.size()));
                return static_cast<bool>(file);
            })) {
            file.close();
            remove_quietly(dir);
            return fail(e->name + ": " + zip.error());
        }
        file.close();
        if (!file) {
            remove_quietly(dir);
            return fail("cannot write " + to.string());
        }
#ifndef _WIN32
        // A plugin's library as the loader wants it; nothing else is made runnable.
        if (e == library)
            fs::permissions(to, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                            fs::perm_options::add, ec);
#endif
        f.sha256 = h.finish();
        out.push_back(std::move(f));
    }
    return out;
}

bool apply_addon(const fs::path& data_dir, const InstalledAddon& addon, std::string* error) {
    const fs::path dir = addons_dir(data_dir);
    const fs::path src = dir / "new" / rel(addon.name);
    const fs::path prev = dir / "previous" / rel(addon.name);
    const fs::path target = addon_folder(data_dir, addon.kind, addon.name);
    std::vector<InstalledAddon> all = read_installed_addons(data_dir);
    const InstalledAddon* old = find_addon(all, addon.name);
    if (old && old->kind != addon.kind) {
        if (error) *error = "an add-on cannot change from a script to a plugin or back";
        return false;
    }
    std::error_code ec;
    for (const auto& f : addon.files)
        if (!fs::is_regular_file(src / rel(f.path), ec)) {
            if (error) *error = "the unpacked package is gone: download it again";
            return false;
        }
    remove_quietly(prev);
    fs::create_directories(dir, ec);
    Journal journal(data_dir);
    if (!journal.ok()) {
        if (error) *error = "cannot write the add-ons' journal";
        return false;
    }
    auto undo = [&](const std::string& why) {
        journal.close();
        undo_journal(data_dir);
        remove_quietly(dir / "journal.txt");
        remove_quietly(prev);
        log::warn("Add-ons: {} not installed: {}", addon.name, why);
        if (error) *error = why;
        return false;
    };
    // Out: every file the earlier version shipped and every file this one ships that is there now.
    std::vector<std::string> out;
    if (old)
        for (const auto& f : old->files) out.push_back(f.path);
    for (const auto& f : addon.files)
        if (std::find(out.begin(), out.end(), f.path) == out.end()) out.push_back(f.path);
    std::string why;
    for (const auto& p : out) {
        if (!fs::exists(target / rel(p), ec)) continue;
        if (!journal.move(target / rel(p), prev / rel(p), &why)) return undo(why);
    }
    for (const auto& f : addon.files)
        if (!journal.move(src / rel(f.path), target / rel(f.path), &why)) return undo(why);
    InstalledAddon entry = addon;
    // An update keeps the player's auto-update switch as installed.txt has it now (they may have changed it while
    // the update downloaded).
    if (old) entry.auto_update = old->auto_update;
    std::erase_if(all, [&](const InstalledAddon& a) { return iequal(a.name, addon.name); });
    all.push_back(std::move(entry));
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return lower(a.name) < lower(b.name); });
    if (!write_installed_addons(data_dir, all)) return undo("cannot write addons/installed.txt");
    journal.done();
    journal.close();
    remove_quietly(dir / "journal.txt");
    remove_quietly(prev);
    remove_quietly(src);
    remove_empty_dirs(target, false);
    log::info("Add-ons: {} {} installed into {}", addon.name, addon.version, target.string());
    return true;
}

bool remove_addon(const fs::path& data_dir, std::string_view name, std::string* error) {
    const fs::path dir = addons_dir(data_dir);
    std::vector<InstalledAddon> all = read_installed_addons(data_dir);
    const InstalledAddon* found = find_addon(all, name);
    if (!found) {
        if (error) *error = "no add-on is named " + std::string(name);
        return false;
    }
    const InstalledAddon addon = *found;
    const fs::path prev = dir / "previous" / rel(addon.name);
    const fs::path target = addon_folder(data_dir, addon.kind, addon.name);
    remove_quietly(prev);
    Journal journal(data_dir);
    if (!journal.ok()) {
        if (error) *error = "cannot write the add-ons' journal";
        return false;
    }
    std::string why;
    std::error_code ec;
    bool ok = true;
    for (const auto& f : addon.files) {
        if (!fs::exists(target / rel(f.path), ec)) continue;
        if (!journal.move(target / rel(f.path), prev / rel(f.path), &why)) {
            ok = false;
            break;
        }
    }
    std::erase_if(all, [&](const InstalledAddon& a) { return iequal(a.name, addon.name); });
    if (ok && !write_installed_addons(data_dir, all)) {
        ok = false;
        why = "cannot write addons/installed.txt";
    }
    if (!ok) {
        journal.close();
        undo_journal(data_dir);
        remove_quietly(dir / "journal.txt");
        remove_quietly(prev);
        if (error) *error = why;
        return false;
    }
    journal.done();
    journal.close();
    remove_quietly(dir / "journal.txt");
    remove_quietly(prev);
    remove_empty_dirs(target, true);
    log::info("Add-ons: {} removed", addon.name);
    return true;
}

std::vector<plugin::Ceiling> addon_ceilings(const std::vector<InstalledAddon>& addons) {
    std::vector<plugin::Ceiling> out;
    for (const auto& a : addons)
        for (const auto& f : a.files) {
            const std::string ext = extension_of(f.path);
            if (a.kind == AddonKind::Script && ext == ".lua") out.push_back({plugin::FileKind::Script, f.sha256, a.caps});
            else if (a.kind == AddonKind::Plugin && (ext == ".so" || ext == ".dll" || ext == ".dylib") &&
                     f.path.find('/') == std::string::npos &&
                     iequal(plugin::plugin_name(rel(f.path)), a.name))
                out.push_back({plugin::FileKind::Plugin, f.sha256, a.caps});
        }
    return out;
}

void addons_at_start(const fs::path& data_dir) {
    const fs::path dir = addons_dir(data_dir);
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    if (fs::exists(dir / "journal.txt", ec)) {
        if (!journal_done(data_dir)) {
            log::warn("Add-ons: an install was cut off; it is undone");
            undo_journal(data_dir);
        }
        remove_quietly(dir / "journal.txt");
    }
    remove_quietly(dir / "previous");
    remove_quietly(dir / "new");
}

} // namespace nmsc::app
