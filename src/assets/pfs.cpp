#include "assets/pfs.h"

#include <algorithm>
#include <array>
#include <stdexcept>

#include <zlib.h>

#include "core/byte_io.h"
#include "core/game_dir.h"

namespace nmsc::assets {

namespace {
constexpr std::uint32_t kMagic = 0x20534650; // "PFS "
constexpr std::uint32_t kNameTableCrc = 0x61580ac9;
// The most a zlib stream expands: deflate's limit is about 1032:1 (real archives stay under 300:1).
constexpr std::uint64_t kMaxDeflateRatio = 1032;

constexpr std::array<std::uint32_t, 256> make_crc_table() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i << 24;
        for (int k = 0; k < 8; ++k) c = (c & 0x80000000u) ? (c << 1) ^ 0x04c11db7u : c << 1;
        t[i] = c;
    }
    return t;
}
constexpr auto kCrcTable = make_crc_table();
} // namespace

std::uint32_t pfs_name_crc(std::string_view name) {
    std::uint32_t c = 0;
    auto step = [&](unsigned char b) { c = (c << 8) ^ kCrcTable[((c >> 24) ^ b) & 0xff]; };
    for (char ch : name) step(static_cast<unsigned char>(ch));
    step(0);
    return c;
}

void zlib_inflate(std::span<const std::byte> in, std::span<std::byte> out) {
    z_stream z{};
    z.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(in.data()));
    z.avail_in = static_cast<uInt>(in.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = static_cast<uInt>(out.size());
    if (inflateInit(&z) != Z_OK) throw std::runtime_error("inflateInit failed");
    int r = inflate(&z, Z_FINISH);
    std::size_t produced = out.size() - z.avail_out;
    inflateEnd(&z);
    if (r != Z_STREAM_END || produced != out.size())
        throw std::runtime_error("zlib block did not inflate to its declared size");
}

PfsArchive PfsArchive::open(const std::filesystem::path& path) {
    auto data = read_file(path);
    if (!data) throw std::runtime_error("cannot read " + path.string());
    return from_memory(std::move(*data), path.filename().string());
}

PfsArchive PfsArchive::from_memory(std::vector<std::byte> data, std::string label) {
    PfsArchive a;
    a.label_ = std::move(label);
    a.data_ = std::move(data);

    ByteReader r(a.data_);
    auto dir_offset = r.le<std::uint32_t>();
    if (r.le<std::uint32_t>() != kMagic) throw std::runtime_error(a.label_ + ": not a PFS archive");
    r.seek(dir_offset);
    auto count = r.le<std::uint32_t>();
    if (count > r.remaining() / 12) throw std::runtime_error(a.label_ + ": bad PFS directory count");

    std::vector<Entry> entries(count);
    for (auto& e : entries) {
        e.crc = r.le<std::uint32_t>();
        e.offset = r.le<std::uint32_t>();
        e.size = r.le<std::uint32_t>();
    }

    auto table = std::find_if(entries.begin(), entries.end(), [](const Entry& e) { return e.crc == kNameTableCrc; });
    if (table == entries.end())
        table = std::max_element(entries.begin(), entries.end(),
                                 [](const Entry& x, const Entry& y) { return x.offset < y.offset; });
    if (table == entries.end()) throw std::runtime_error(a.label_ + ": PFS archive has no name table");
    Entry table_entry = *table;
    entries.erase(table);

    std::unordered_map<std::uint32_t, Entry> by_crc;
    for (const auto& e : entries) by_crc.emplace(e.crc, e);

    auto table_data = a.inflate_entry(table_entry);
    ByteReader tr(table_data);
    auto name_count = tr.le<std::uint32_t>();
    for (std::uint32_t i = 0; i < name_count; ++i) {
        auto len = tr.le<std::uint32_t>();
        std::string key = GameDir::normalize(tr.fixed_string(len));
        auto it = by_crc.find(pfs_name_crc(key));
        if (it == by_crc.end()) continue;
        a.names_.push_back(key);
        a.by_name_.emplace(std::move(key), it->second);
    }
    std::sort(a.names_.begin(), a.names_.end(), [&](const std::string& x, const std::string& y) {
        return a.by_name_.at(x).offset < a.by_name_.at(y).offset;
    });
    return a;
}

bool PfsArchive::contains(std::string_view name) const { return by_name_.contains(GameDir::normalize(name)); }

std::optional<std::vector<std::byte>> PfsArchive::read(std::string_view name) const {
    auto it = by_name_.find(GameDir::normalize(name));
    if (it == by_name_.end()) return std::nullopt;
    return inflate_entry(it->second);
}

std::vector<std::byte> PfsArchive::inflate_entry(const Entry& e) const {
    // The block headers are walked before anything is allocated, so the size a file declares is trusted only as far
    // as its compressed bytes back it: each block expands by at most kMaxDeflateRatio, and the blocks add up to
    // exactly the file's size. (Otherwise a 2 MB archive declaring a 2 GB file allocates 2 GB.)
    ByteReader r(data_);
    r.seek(e.offset);
    for (std::uint64_t total = 0; total < e.size;) {
        auto deflated = r.le<std::uint32_t>();
        auto inflated = r.le<std::uint32_t>();
        if (inflated > e.size - total) throw std::runtime_error(label_ + ": PFS block overruns its file");
        if (inflated > std::uint64_t{deflated} * kMaxDeflateRatio)
            throw std::runtime_error(label_ + ": PFS block is larger than its compressed bytes can hold");
        r.skip(deflated);
        total += inflated;
    }
    std::vector<std::byte> out(e.size);
    r.seek(e.offset);
    std::size_t filled = 0;
    while (filled < out.size()) {
        auto deflated = r.le<std::uint32_t>();
        auto inflated = r.le<std::uint32_t>();
        zlib_inflate(r.bytes(deflated), std::span(out).subspan(filled, inflated));
        filled += inflated;
    }
    return out;
}

} // namespace nmsc::assets
