#pragma once

// PFS archives: .s3d, .eqg, .pak. Layout (OpenEQ docs/ARCHITECTURE.md, and
// widely documented by community tools):
//   u32 directory offset, "PFS " magic, u32 version (0x20000)
//   at the directory offset: u32 count, then count × {u32 crc, u32 offset, u32 size}
//   each file is a run of {u32 deflated, u32 inflated, zlib stream} blocks
// Each directory crc is pfs_name_crc() of the lower-case file name, and the
// file-name table is the entry with crc 0x61580ac9 (or, in some third-party
// archives, 0xffffffff at the highest offset). Names are matched to entries by
// crc. Some shipped archives list a name with no entry (greatdivide_chr.s3d,
// velketor_chr.s3d); those names are dropped.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hailstone::assets {

class PfsArchive {
public:
    // Throws std::runtime_error on a malformed archive.
    static PfsArchive open(const std::filesystem::path& path);
    static PfsArchive from_memory(std::vector<std::byte> data, std::string label = "<memory>");

    // File names as stored (lower case), in data-offset order.
    const std::vector<std::string>& names() const { return names_; }
    bool contains(std::string_view name) const;

    // Inflated contents of a file (name matched case-insensitively).
    std::optional<std::vector<std::byte>> read(std::string_view name) const;

    const std::string& label() const { return label_; }

private:
    struct Entry {
        std::uint32_t crc = 0;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    std::vector<std::byte> inflate_entry(const Entry& e) const;

    std::string label_;
    std::vector<std::byte> data_;
    std::vector<std::string> names_;
    std::unordered_map<std::string, Entry> by_name_;
};

// CRC-32 (polynomial 0x04c11db7, not reflected, initial value 0) over the name
// including its NUL terminator.
std::uint32_t pfs_name_crc(std::string_view name);

// Inflates one zlib stream into exactly `out.size()` bytes. Throws on error.
void zlib_inflate(std::span<const std::byte> in, std::span<std::byte> out);

} // namespace hailstone::assets
