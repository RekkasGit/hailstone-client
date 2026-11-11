#include "net/zone_link_packets.h"

#include "core/byte_io.h"
#include "net/packet_io.h"

namespace hailstone::net {

using detail::decode_with;

AppPacket encode_zone_links_request() {
    ByteWriter w;
    w.le(kZoneLinksMagic);
    w.le(kZoneLinksVersion);
    return AppPacket(kOpHsZoneLinksRequest, w.take());
}

std::optional<ZoneLinks> decode_zone_links(const AppPacket& p) {
    if (p.opcode != kOpHsZoneLinks) return std::nullopt;
    auto got = decode_with(p, [](ByteReader& r) -> std::optional<ZoneLinks> {
        if (r.le<std::uint32_t>() != kZoneLinksMagic || r.le<std::uint16_t>() != kZoneLinksVersion) return std::nullopt;
        ZoneLinks v;
        v.flags = r.le<std::uint16_t>();
        const auto count = r.le<std::uint32_t>();
        if (count > kZoneLinksMax || r.remaining() / 4 < count) return std::nullopt;
        v.links.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto from = r.le<std::uint16_t>();
            const auto to = r.le<std::uint16_t>();
            v.links.emplace_back(from, to);
        }
        return v;
    });
    return got ? *got : std::nullopt;
}

AppPacket encode(const ZoneLinks& v) {
    ByteWriter w;
    w.le(kZoneLinksMagic);
    w.le(kZoneLinksVersion);
    w.le(v.flags);
    w.le(static_cast<std::uint32_t>(v.links.size()));
    for (const auto& [from, to] : v.links) {
        w.le(from);
        w.le(to);
    }
    return AppPacket(kOpHsZoneLinks, w.take());
}

} // namespace hailstone::net
