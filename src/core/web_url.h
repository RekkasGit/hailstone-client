#pragma once

// Web addresses the client opens for a script, a plugin or the server (docs/browser.md): only http and https pages
// with a host, and, when the server's hailstone.cfg names hosts ([web] allow), only those. The player's own /url
// is not held to these.

#include <string>
#include <string_view>
#include <vector>

namespace hailstone::web {

// The host of an http:// or https:// address, lower-cased, without user info or port ("" when the address is not
// one: another scheme, no host, a space or control character anywhere in it, or a host with a character outside
// letters, digits, '-', '_' and '.', or an IPv6 literal's). '\' ends the host as '/' does, as browsers read it.
std::string web_host(std::string_view url);

// `host` is one of `allow` or below one ("example.org" allows "www.example.org"); an empty list allows any host.
bool host_allowed(std::string_view host, const std::vector<std::string>& allow);

// An http(s) address whose host `allow` lets through.
inline bool url_allowed(std::string_view url, const std::vector<std::string>& allow) {
    std::string host = web_host(url);
    return !host.empty() && host_allowed(host, allow);
}

} // namespace hailstone::web
