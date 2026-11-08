#include "core/web_url.h"

#include <cctype>

namespace hailstone::web {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool ascii_alnum(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool ascii_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

bool starts_no_case(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (lower(s[i]) != prefix[i]) return false;
    return true;
}

} // namespace

std::string web_host(std::string_view url) {
    for (char c : url)
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f) return {};
    std::string_view rest;
    if (starts_no_case(url, "https://")) rest = url.substr(8);
    else if (starts_no_case(url, "http://")) rest = url.substr(7);
    else return {};
    // WHATWG URL parsing ends an http(s) authority at '\' as at '/', so "https://evil.example\@good.org/" goes to
    // evil.example in a browser and must not read as good.org here.
    std::string_view authority = rest.substr(0, rest.find_first_of("/\\?#"));
    if (std::size_t at = authority.rfind('@'); at != std::string_view::npos) authority.remove_prefix(at + 1);
    std::string_view host = authority;
    if (host.starts_with('[')) { // an IPv6 literal, kept with its brackets
        std::size_t close = host.find(']');
        if (close == std::string_view::npos) return {};
        if (close + 1 < host.size() && host[close + 1] != ':') return {}; // only a port may follow
        host = host.substr(0, close + 1);
        for (char c : host.substr(1, host.size() - 2))
            if (!ascii_hex(c) && c != ':' && c != '.') return {};
    } else {
        host = host.substr(0, host.find(':'));
        // A host name of letters, digits, '-', '_' and '.' only: anything else ('%' escapes, '^', '|', '<', non-ASCII
        // bytes a browser would map through IDNA) could name another host to a browser than it does here. An
        // international name is given in its xn-- form.
        for (char c : host)
            if (!ascii_alnum(c) && c != '-' && c != '_' && c != '.') return {};
    }
    if (host.empty() || host == "[]") return {};
    std::string out;
    out.reserve(host.size());
    for (char c : host) out.push_back(lower(c));
    while (!out.empty() && out.back() == '.') out.pop_back(); // "example.org." is example.org
    return out;
}

bool host_allowed(std::string_view host, const std::vector<std::string>& allow) {
    if (allow.empty()) return true;
    for (const std::string& a : allow) {
        if (a.empty()) continue;
        if (host == a) return true;
        if (host.size() > a.size() && host.ends_with(a) && host[host.size() - a.size() - 1] == '.') return true;
    }
    return false;
}

} // namespace hailstone::web
