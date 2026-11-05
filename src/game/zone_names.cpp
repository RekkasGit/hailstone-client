#include "game/zone_names.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>

namespace nmsc::game {

namespace {
struct ZoneName {
    std::uint32_t id;
    const char* short_name;
    const char* long_name;
    std::uint32_t flags;
    std::uint32_t expansion;
};
constexpr ZoneName kZones[] = {
#define NMSC_ZONE(id, short_name, long_name, flags, expansion) {id, short_name, long_name, flags, expansion},
#include "game/zone_names.inc"
#undef NMSC_ZONE
};

const ZoneName* find(std::uint32_t id) {
    auto it = std::lower_bound(std::begin(kZones), std::end(kZones), id,
                               [](const ZoneName& z, std::uint32_t v) { return z.id < v; });
    return it != std::end(kZones) && it->id == id ? it : nullptr;
}
} // namespace

std::string zone_short_name(std::uint32_t zone_id) {
    auto z = find(zone_id);
    return z ? z->short_name : std::string();
}

std::string zone_long_name(std::uint32_t zone_id) {
    auto z = find(zone_id);
    return z ? z->long_name : std::string();
}

std::uint32_t zone_id_by_short_name(std::string_view short_name) {
    for (const auto& z : kZones) {
        std::string_view n = z.short_name;
        if (n.size() == short_name.size() &&
            std::equal(n.begin(), n.end(), short_name.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }))
            return z.id;
    }
    return 0;
}

std::uint32_t zone_flags(std::uint32_t zone_id) {
    auto z = find(zone_id);
    return z ? z->flags : 0;
}

std::uint32_t zone_expansion(std::uint32_t zone_id) {
    if (zone_id > 1000) zone_id &= 0x7fff;
    auto z = find(zone_id);
    return z ? z->expansion : 0;
}

bool is_newbie_zone(std::uint32_t zone_id) {
    if (zone_id > 1000) zone_id &= 0x7fff;
    return (zone_flags(zone_id) & 0x2) != 0;
}

} // namespace nmsc::game
