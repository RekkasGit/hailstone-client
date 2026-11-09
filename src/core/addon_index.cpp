#include "core/addon_index.h"

#include <charconv>
#include <system_error>

#include "core/addon_manifest.h"
#include "core/addon_source.h"
#include "core/update_manifest.h"

namespace hailstone {

namespace {

constexpr std::size_t kMaxIndex = 64 * 1024; // as the add-on manager fetches it
constexpr std::size_t kMaxRecords = 4096;
constexpr std::string_view kSignature = "signature ";

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// The next space-separated field of `line` from `at`; `at` moves past it.
std::string_view next_field(std::string_view line, std::size_t& at) {
    while (at < line.size() && line[at] == ' ') ++at;
    const std::size_t start = at;
    while (at < line.size() && line[at] != ' ') ++at;
    return line.substr(start, at - start);
}

// A login server's host: letters, digits, '-' and '.', as a server profile's address is typed.
bool host_ok(std::string_view s) {
    if (s.empty() || s.size() > 253) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.'))
            return false;
    return true;
}

// A world's name as a login server lists it: printable, one line, at most 64 bytes (the server list's field).
bool world_ok(std::string_view s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s)
        if (static_cast<unsigned char>(c) < ' ' || c == 0x7f) return false;
    return true;
}

std::nullopt_t fail(std::string* error, std::string why) {
    if (error) *error = std::move(why);
    return std::nullopt;
}

} // namespace

std::optional<AddonIndex> parse_addon_index(std::string_view text, const Ed25519PublicKey& key, std::string* error) {
    if (text.size() > kMaxIndex) return fail(error, "the add-on index is too large");
    // The same signature line as the update manifest's (core/update_manifest.h).
    const std::string_view body = update_manifest_signed_part(text);
    if (body.size() == text.size()) return fail(error, "the add-on index is not signed");
    std::string_view sig_line = text.substr(body.size());
    if (!sig_line.empty() && sig_line.back() == '\n') sig_line.remove_suffix(1);
    if (!sig_line.empty() && sig_line.back() == '\r') sig_line.remove_suffix(1);
    if (sig_line.find('\n') != std::string_view::npos) return fail(error, "the add-on index has lines after its signature");
    auto signature = hex_decode<64>(sig_line.substr(kSignature.size()));
    if (!signature) return fail(error, "the add-on index's signature is not readable");
    if (!ed25519_verify(*signature, key, body))
        return fail(error, "the add-on index's signature does not match the release key");

    AddonIndex index;
    bool header = false;
    std::size_t at = 0;
    int line_no = 0;
    while (at < body.size()) {
        std::size_t nl = body.find('\n', at);
        if (nl == std::string_view::npos) nl = body.size();
        std::string_view line = body.substr(at, nl - at);
        at = nl + 1;
        ++line_no;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        std::size_t pos = 0;
        const std::string_view record = next_field(line, pos);
        if (record.empty() || record[0] == '#') continue;
        const std::string where = "line " + std::to_string(line_no) + " of the add-on index";
        if (!header) {
            const std::string_view kind = next_field(line, pos);
            if (record != "hailstone-addon-index" || !trim(line.substr(pos)).empty())
                return fail(error, "this is not an add-on index");
            if (kind != "1") return fail(error, "the add-on index is a newer kind (" + std::string(kind) + ")");
            header = true;
            continue;
        }
        if (index.addons.size() + index.worlds.size() >= kMaxRecords)
            return fail(error, "the add-on index has too many records");
        if (record == "addon") {
            const std::string_view name = next_field(line, pos), source = next_field(line, pos),
                                   key_hex = next_field(line, pos);
            if (!trim(line.substr(pos)).empty()) return fail(error, where + " is not an add-on");
            auto src = parse_addon_link(source);
            auto k = hex_decode<32>(key_hex);
            if (!addon_name_ok(name) || !src || !src->tag.empty() || !src->branch.empty() || !k)
                return fail(error, where + " is not an add-on");
            for (const auto& a : index.addons)
                if (lower(a.name) == lower(name)) return fail(error, where + " repeats an add-on");
            index.addons.push_back({std::string(name), addon_source_text(*src), *k});
        } else if (record == "serial") {
            const std::string_view n = next_field(line, pos);
            std::uint64_t v = 0;
            auto [end, ec] = std::from_chars(n.data(), n.data() + n.size(), v);
            if (index.serial || n.empty() || ec != std::errc{} || end != n.data() + n.size() || v == 0 ||
                !trim(line.substr(pos)).empty())
                return fail(error, where + " is not a serial");
            index.serial = v;
        } else if (record == "world") {
            const std::string_view name = next_field(line, pos), login = next_field(line, pos);
            const std::string_view world = trim(line.substr(pos));
            if (!addon_name_ok(name) || (login != "*" && !host_ok(login)) || !world_ok(world))
                return fail(error, where + " is not a world");
            bool known = false;
            for (const auto& a : index.addons) known = known || lower(a.name) == lower(name);
            if (!known) return fail(error, where + " names an add-on the index has not listed above it");
            index.worlds.push_back({std::string(name), login == "*" ? std::string() : lower(login), std::string(world)});
        }
        // Any other record is a later client's.
    }
    if (!header) return fail(error, "this is not an add-on index");
    return index;
}

std::vector<AddonIndexMatch> addon_index_matches(const AddonIndex& index, std::string_view login_host,
                                                 const std::vector<std::string>& worlds) {
    std::string host = lower(trim(login_host));
    while (!host.empty() && host.back() == '.') host.pop_back();
    std::vector<AddonIndexMatch> out;
    for (const auto& a : index.addons) {
        const std::string* match = nullptr;
        for (const auto& listed : worlds) {
            for (const auto& w : index.worlds)
                if (!match && lower(w.addon) == lower(a.name) && (w.login.empty() || w.login == host) &&
                    lower(trim(listed)) == lower(w.world))
                    match = &listed;
        }
        if (match) out.push_back({a, *match});
    }
    return out;
}

std::string addon_index_older(const std::optional<std::uint64_t>& kept, const std::optional<std::uint64_t>& fetched) {
    if (!kept) return {};
    if (!fetched) return "it has no serial, and the one kept has " + std::to_string(*kept);
    if (*fetched < *kept)
        return "its serial " + std::to_string(*fetched) + " is older than the one kept (" + std::to_string(*kept) + ")";
    return {};
}

} // namespace hailstone
