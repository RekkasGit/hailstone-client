#include "game/zone_routes.h"

#include <algorithm>
#include <charconv>
#include <deque>

namespace hailstone::game {

namespace {

// Breadth first over `next(zone)` (each ascending), so the first route found is the shortest and, among equals, the
// one through the lowest ids.
template <class Next>
std::vector<std::uint32_t> search(std::uint32_t from, std::uint32_t to, Next&& next) {
    std::map<std::uint32_t, std::uint32_t> came_from{{from, from}};
    std::deque<std::uint32_t> queue{from};
    while (!queue.empty()) {
        const std::uint32_t z = queue.front();
        queue.pop_front();
        if (z == to) break;
        next(z, [&](std::uint32_t n) {
            if (came_from.emplace(n, z).second) queue.push_back(n);
        });
    }
    if (!came_from.contains(to)) return {};
    std::vector<std::uint32_t> path;
    for (std::uint32_t z = to;; z = came_from[z]) {
        path.push_back(z);
        if (z == from) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

bool read_u32(std::string_view& s, std::uint32_t& out) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    if (ec != std::errc{} || end == s.data()) return false;
    s.remove_prefix(static_cast<std::size_t>(end - s.data()));
    return true;
}

} // namespace

bool ZoneRoutes::learn(std::uint32_t from, const std::vector<std::uint32_t>& to) {
    if (from == 0) return false;
    bool added = false;
    for (std::uint32_t t : to)
        if (t != 0 && t != from && learned_.emplace(from, t).second) added = true;
    if (added) rebuild();
    return added;
}

bool ZoneRoutes::set_server(std::vector<Link> links) {
    std::set<Link> next;
    for (const auto& l : links)
        if (l.first != 0 && l.second != 0 && l.first != l.second) next.insert(l);
    if (next == server_) return false;
    server_ = std::move(next);
    rebuild();
    return true;
}

void ZoneRoutes::rebuild() {
    forward_.clear();
    backward_.clear();
    for (const auto& l : server_) forward_[l.first].insert(l.second);
    for (const auto& l : learned_) {
        forward_[l.first].insert(l.second);
        backward_[l.second].insert(l.first);
    }
}

bool ZoneRoutes::set_server_zones(std::vector<std::uint32_t> zones, int expansion) {
    std::set<std::uint32_t> next;
    for (std::uint32_t z : zones)
        if (z != 0) next.insert(z);
    if (next == server_zones_ && expansion == server_expansion_) return false;
    server_zones_ = std::move(next);
    server_expansion_ = expansion;
    return true;
}

ZoneRoute ZoneRoutes::route(std::uint32_t from, std::uint32_t to, const std::function<bool(std::uint32_t)>& allowed) const {
    ZoneRoute r;
    if (from == 0 || to == 0) return r;
    if (from != to && allowed && !allowed(to)) return r;
    auto ok = [&](std::uint32_t z) { return !allowed || allowed(z); };
    if (from == to) {
        r.zones = {from};
        return r;
    }
    auto forward = [&](std::uint32_t z, auto&& visit) {
        if (auto it = forward_.find(z); it != forward_.end())
            for (std::uint32_t n : it->second)
                if (ok(n)) visit(n);
    };
    r.zones = search(from, to, forward);
    if (!r.zones.empty()) return r;
    // Both ways (a merged ascending list, so the order stays the lowest id first).
    auto both = [&](std::uint32_t z, auto&& visit) {
        std::set<std::uint32_t> all;
        if (auto it = forward_.find(z); it != forward_.end()) all.insert(it->second.begin(), it->second.end());
        if (auto it = backward_.find(z); it != backward_.end()) all.insert(it->second.begin(), it->second.end());
        for (std::uint32_t n : all)
            if (ok(n)) visit(n);
    };
    r.zones = search(from, to, both);
    r.assumed = !r.zones.empty();
    return r;
}

std::vector<std::uint32_t> ZoneRoutes::neighbours(std::uint32_t zone) const {
    auto it = forward_.find(zone);
    if (it == forward_.end()) return {};
    return {it->second.begin(), it->second.end()};
}

std::vector<std::uint32_t> ZoneRoutes::zones() const {
    std::set<std::uint32_t> all;
    for (const auto* links : {&server_, &learned_})
        for (const auto& l : *links) {
            all.insert(l.first);
            all.insert(l.second);
        }
    return {all.begin(), all.end()};
}

std::string ZoneRoutes::serialize() const {
    std::string out = "# Hailstone zone links: s = the server's list, l = learned from zone points\n";
    for (const auto& l : server_) out += "s " + std::to_string(l.first) + " " + std::to_string(l.second) + "\n";
    for (const auto& l : learned_) out += "l " + std::to_string(l.first) + " " + std::to_string(l.second) + "\n";
    if (!server_zones_.empty()) {
        out += "e " + std::to_string(server_expansion_) + "\n";
        for (std::uint32_t z : server_zones_) out += "z " + std::to_string(z) + "\n";
    }
    return out;
}

ZoneRoutes ZoneRoutes::parse(std::string_view text) {
    ZoneRoutes r;
    while (!text.empty()) {
        std::size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        if (line.empty()) continue;
        const char kind = line.front();
        line.remove_prefix(1);
        if (kind == 'z') {
            std::uint32_t z = 0;
            if (read_u32(line, z) && z != 0) r.server_zones_.insert(z);
            continue;
        }
        if (kind == 'e') {
            while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
            int e = 0;
            auto [end, ec] = std::from_chars(line.data(), line.data() + line.size(), e);
            if (ec == std::errc{} && end != line.data()) r.server_expansion_ = e;
            continue;
        }
        if (kind != 's' && kind != 'l') continue;
        std::uint32_t from = 0, to = 0;
        if (!read_u32(line, from) || !read_u32(line, to) || from == 0 || to == 0 || from == to) continue;
        (kind == 's' ? r.server_ : r.learned_).emplace(from, to);
    }
    r.rebuild();
    return r;
}

} // namespace hailstone::game
