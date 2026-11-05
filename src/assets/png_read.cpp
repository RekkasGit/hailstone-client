#include "assets/png_read.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

#include <zlib.h>

namespace nmsc::assets {

namespace {

constexpr std::array<unsigned char, 8> kSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
constexpr std::uint32_t kMaxSide = 16384;
// At most 8192 x 8192 pixels (256 MiB of RGBA): more than any replacement texture needs, and a bound on what a
// forged header can make the decoder allocate.
constexpr std::uint64_t kMaxPixels = std::uint64_t{8192} * 8192;
// A zlib stream expands by at most ~1032:1 (as pfs.cpp checks), so scanlines beyond that of the IDAT data are a
// bogus header, not data.
constexpr std::size_t kMaxDeflateRatio = 1032;

std::uint32_t be32(const unsigned char* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

struct Header {
    std::uint32_t width = 0, height = 0;
    int depth = 0, color = 0;
    bool interlaced = false;
    int channels() const { return color == 0 ? 1 : color == 2 ? 3 : color == 3 ? 1 : color == 4 ? 2 : 4; }
    // Bits per pixel, and bytes per pixel as the filters count them (at least 1).
    int bits() const { return depth * channels(); }
    std::size_t filter_bytes() const { return static_cast<std::size_t>(bits() < 8 ? 1 : bits() / 8); }
    std::size_t row_bytes(std::uint32_t w) const { return (std::size_t(w) * static_cast<std::size_t>(bits()) + 7) / 8; }
};

int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

// Undoes the filters of a w x h sub-image whose filtered rows start at `in`; false on a bad filter type.
bool unfilter(const Header& h, std::uint32_t w, std::uint32_t rows, const unsigned char* in,
              std::vector<unsigned char>& out) {
    std::size_t stride = h.row_bytes(w), bpp = h.filter_bytes();
    out.assign(stride * rows, 0);
    for (std::uint32_t y = 0; y < rows; ++y) {
        unsigned char type = in[y * (stride + 1)];
        const unsigned char* src = in + y * (stride + 1) + 1;
        unsigned char* cur = out.data() + y * stride;
        const unsigned char* prev = y > 0 ? cur - stride : nullptr;
        for (std::size_t x = 0; x < stride; ++x) {
            int a = x >= bpp ? cur[x - bpp] : 0;
            int b = prev ? prev[x] : 0;
            int c = prev && x >= bpp ? prev[x - bpp] : 0;
            int v = src[x];
            switch (type) {
            case 0: break;
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: v += paeth(a, b, c); break;
            default: return false;
            }
            cur[x] = static_cast<unsigned char>(v);
        }
    }
    return true;
}

// Sample `i` of a row (in units of the bit depth), scaled to 8 bits.
int sample(const Header& h, const unsigned char* row, std::size_t i) {
    switch (h.depth) {
    case 16: return row[i * 2];
    case 8: return row[i];
    default: {
        std::size_t bit = i * static_cast<std::size_t>(h.depth);
        int v = (row[bit / 8] >> (8 - h.depth - static_cast<int>(bit % 8))) & ((1 << h.depth) - 1);
        return v * 255 / ((1 << h.depth) - 1);
    }
    }
}

// A raw sample, unscaled (palette indices; tRNS keys).
int raw_sample(const Header& h, const unsigned char* row, std::size_t i) {
    if (h.depth == 16) return (row[i * 2] << 8) | row[i * 2 + 1];
    if (h.depth == 8) return row[i];
    std::size_t bit = i * static_cast<std::size_t>(h.depth);
    return (row[bit / 8] >> (8 - h.depth - static_cast<int>(bit % 8))) & ((1 << h.depth) - 1);
}

} // namespace

bool is_png(std::span<const std::byte> data) {
    return data.size() >= kSignature.size() && std::memcmp(data.data(), kSignature.data(), kSignature.size()) == 0;
}

std::optional<Image> decode_png(std::span<const std::byte> data, std::string* error) {
    auto fail = [&](const char* why) -> std::optional<Image> {
        if (error) *error = why;
        return std::nullopt;
    };
    if (!is_png(data)) return fail("not a PNG");
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    Header h;
    bool have_header = false;
    std::vector<unsigned char> idat;
    std::array<std::array<unsigned char, 4>, 256> palette{};
    for (auto& p : palette) p = {0, 0, 0, 255};
    std::size_t palette_size = 0;
    std::optional<std::array<int, 3>> key; // tRNS for grey (one value) and RGB
    for (std::size_t pos = kSignature.size(); pos + 12 <= data.size();) {
        std::uint32_t len = be32(bytes + pos);
        if (len > data.size() - pos - 12) return fail("truncated chunk");
        const unsigned char* type = bytes + pos + 4;
        const unsigned char* body = bytes + pos + 8;
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len < 13) return fail("short IHDR");
            h.width = be32(body);
            h.height = be32(body + 4);
            h.depth = body[8];
            h.color = body[9];
            h.interlaced = body[12] == 1;
            if (body[10] != 0 || body[11] != 0 || body[12] > 1) return fail("unknown compression, filter or interlace");
            bool ok_depth = (h.color == 0 && (h.depth == 1 || h.depth == 2 || h.depth == 4 || h.depth == 8 || h.depth == 16)) ||
                            (h.color == 3 && (h.depth == 1 || h.depth == 2 || h.depth == 4 || h.depth == 8)) ||
                            ((h.color == 2 || h.color == 4 || h.color == 6) && (h.depth == 8 || h.depth == 16));
            if (!ok_depth) return fail("unsupported colour type or bit depth");
            if (h.width == 0 || h.height == 0 || h.width > kMaxSide || h.height > kMaxSide ||
                std::uint64_t{h.width} * h.height > kMaxPixels)
                return fail("bad size");
            have_header = true;
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            palette_size = std::min<std::size_t>(len / 3, 256);
            for (std::size_t i = 0; i < palette_size; ++i)
                palette[i] = {body[i * 3], body[i * 3 + 1], body[i * 3 + 2], 255};
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            if (h.color == 3) {
                for (std::size_t i = 0; i < len && i < 256; ++i) palette[i][3] = body[i];
            } else if (h.color == 0 && len >= 2) {
                key = std::array<int, 3>{(body[0] << 8) | body[1], 0, 0};
            } else if (h.color == 2 && len >= 6) {
                key = std::array<int, 3>{(body[0] << 8) | body[1], (body[2] << 8) | body[3], (body[4] << 8) | body[5]};
            }
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), body, body + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + len;
    }
    if (!have_header) return fail("no IHDR");
    if (h.color == 3 && palette_size == 0) return fail("no palette");

    // The passes: one for a plain image, Adam7's seven for an interlaced one (start x, y; step x, y).
    struct Pass { std::uint32_t x0, y0, dx, dy; };
    static constexpr Pass kAdam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                       {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    static constexpr Pass kPlain[1] = {{0, 0, 1, 1}};
    std::span<const Pass> passes = h.interlaced ? std::span<const Pass>(kAdam7) : std::span<const Pass>(kPlain);
    std::size_t expected = 0;
    for (const Pass& p : passes) {
        std::uint32_t w = h.width > p.x0 ? (h.width - p.x0 + p.dx - 1) / p.dx : 0;
        std::uint32_t rows = h.height > p.y0 ? (h.height - p.y0 + p.dy - 1) / p.dy : 0;
        if (w && rows) expected += (h.row_bytes(w) + 1) * rows;
    }
    if (expected / kMaxDeflateRatio > idat.size()) return fail("image data too short for its size");
    std::vector<unsigned char> raw(expected);
    uLongf raw_size = static_cast<uLongf>(expected);
    int z = uncompress(raw.data(), &raw_size, idat.data(), static_cast<uLong>(idat.size()));
    if (z != Z_OK || raw_size != expected) return fail("bad image data");

    Image img;
    img.width = h.width;
    img.height = h.height;
    img.format = PixelFormat::RGBA8;
    std::vector<std::byte> px(std::size_t(h.width) * h.height * 4);
    std::size_t offset = 0;
    std::vector<unsigned char> rows_out;
    for (const Pass& p : passes) {
        std::uint32_t w = h.width > p.x0 ? (h.width - p.x0 + p.dx - 1) / p.dx : 0;
        std::uint32_t rows = h.height > p.y0 ? (h.height - p.y0 + p.dy - 1) / p.dy : 0;
        if (!w || !rows) continue;
        if (!unfilter(h, w, rows, raw.data() + offset, rows_out)) return fail("bad filter type");
        offset += (h.row_bytes(w) + 1) * rows;
        std::size_t stride = h.row_bytes(w);
        int ch = h.channels();
        for (std::uint32_t y = 0; y < rows; ++y) {
            const unsigned char* row = rows_out.data() + y * stride;
            for (std::uint32_t x = 0; x < w; ++x) {
                std::array<int, 4> c{};
                std::size_t s = std::size_t(x) * static_cast<std::size_t>(ch);
                switch (h.color) {
                case 0: {
                    int g = sample(h, row, s);
                    bool clear = key && raw_sample(h, row, s) == (*key)[0];
                    c = {g, g, g, clear ? 0 : 255};
                    break;
                }
                case 2: {
                    bool clear = key && raw_sample(h, row, s) == (*key)[0] && raw_sample(h, row, s + 1) == (*key)[1] &&
                                 raw_sample(h, row, s + 2) == (*key)[2];
                    c = {sample(h, row, s), sample(h, row, s + 1), sample(h, row, s + 2), clear ? 0 : 255};
                    break;
                }
                case 3: {
                    const auto& e = palette[static_cast<std::size_t>(raw_sample(h, row, s))];
                    c = {e[0], e[1], e[2], e[3]};
                    break;
                }
                case 4: {
                    int g = sample(h, row, s);
                    c = {g, g, g, sample(h, row, s + 1)};
                    break;
                }
                default: c = {sample(h, row, s), sample(h, row, s + 1), sample(h, row, s + 2), sample(h, row, s + 3)};
                }
                std::size_t at = (std::size_t(p.y0 + y * p.dy) * h.width + p.x0 + x * p.dx) * 4;
                for (int k = 0; k < 4; ++k) px[at + static_cast<std::size_t>(k)] = static_cast<std::byte>(c[k]);
            }
        }
    }
    img.mips.push_back(std::move(px));
    return img;
}

} // namespace nmsc::assets
