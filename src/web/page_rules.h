#pragma once

// What a script's or plugin's page may reach (docs/browser.md "Rules"), beyond core/web_url.h's http(s) and
// [web] allow: never this computer or the player's own network (a router's admin page, a NAS, a dev server on
// localhost), and, inside the page, only the allowed sites for frames and every request (images, scripts, fetch,
// WebSockets) as well as for the page itself. Built into the helper too (hailstone_web_protocol).

#include <string>
#include <string_view>
#include <vector>

namespace hailstone::web {

// A host (core/web_url.h's web_host form: lower case, an IPv6 literal in brackets) that names this computer or a
// private network: localhost and *.localhost, a name without a dot or under .local, .lan, .internal, .home.arpa,
// and IPv4 / IPv6 literals in the loopback, private, link-local, shared (100.64/10), multicast and reserved ranges
// (IPv4 in any form a browser reads: "2130706433", "0x7f.1", "017700000001"; IPv6 with an IPv4 inside). An address
// that does not parse counts as private. A public name that resolves to a private address is not caught here.
bool private_host(std::string_view host);

// An address a script's or plugin's tab may show or open: http(s), on `allow` (empty: any host), not private_host.
bool script_url_allowed(std::string_view url, const std::vector<std::string>& allow);

// A request a script's or plugin's page makes (a frame, an image, a script, fetch, a WebSocket): what
// script_url_allowed lets through (ws:// and wss:// as http:// and https://), and the browser's own local data
// (data:, blob:, about:blank, about:srcdoc), which reach no network.
bool script_request_allowed(std::string_view url, const std::vector<std::string>& allow);

} // namespace hailstone::web
