#include "core/sha1.h"

#include <bit>

namespace nmsc {

Sha1Digest sha1(std::string_view data) {
    std::uint32_t h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    std::string msg(data);
    std::uint64_t bits = static_cast<std::uint64_t>(data.size()) * 8;
    msg += '\x80';
    while (msg.size() % 64 != 56) msg += '\0';
    for (int i = 7; i >= 0; --i) msg += static_cast<char>(bits >> (8 * i));
    for (std::size_t at = 0; at < msg.size(); at += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(msg.data() + at + 4 * i);
            w[i] = std::uint32_t{p[0]} << 24 | std::uint32_t{p[1]} << 16 | std::uint32_t{p[2]} << 8 | p[3];
        }
        for (int i = 16; i < 80; ++i) w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) f = (b & c) | (~b & d), k = 0x5a827999;
            else if (i < 40) f = b ^ c ^ d, k = 0x6ed9eba1;
            else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8f1bbcdc;
            else f = b ^ c ^ d, k = 0xca62c1d6;
            std::uint32_t t = std::rotl(a, 5) + f + e + k + w[i];
            e = d, d = c, c = std::rotl(b, 30), b = a, a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
    }
    Sha1Digest out{};
    for (int i = 0; i < 20; ++i) out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(h[i / 4] >> (24 - 8 * (i % 4)));
    return out;
}

std::string to_hex(const Sha1Digest& d) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    for (auto b : d) s += kHex[b >> 4], s += kHex[b & 0xf];
    return s;
}

} // namespace nmsc
