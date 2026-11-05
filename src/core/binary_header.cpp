#include "core/binary_header.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>

#include "core/byte_io.h"

namespace nmsc {

namespace {

template <typename T> T read(ByteReader& r, bool big) { return big ? r.be<T>() : r.le<T>(); }

bool magic(std::span<const std::byte> b, std::initializer_list<unsigned> m) {
    if (b.size() < m.size()) return false;
    std::size_t i = 0;
    for (unsigned v : m)
        if (std::to_integer<unsigned>(b[i++]) != v) return false;
    return true;
}

// ELF: e_ident, then e_type and e_machine in the file's own byte order.
BinaryHeader elf(std::span<const std::byte> b) {
    BinaryHeader h;
    ByteReader r(b);
    r.skip(5);
    const auto data = r.le<std::uint8_t>(); // EI_DATA: 1 little-endian, 2 big-endian
    if (data != 1 && data != 2) return h;
    r.seek(16);
    const bool big = data == 2;
    const auto type = read<std::uint16_t>(r, big);
    const auto machine = read<std::uint16_t>(r, big);
    h.format = BinaryFormat::Elf;
    h.library = type == 3; // ET_DYN
    switch (machine) {
    case 62: h.archs = {"x64"}; break;    // EM_X86_64
    case 183: h.archs = {"arm64"}; break; // EM_AARCH64
    case 3: h.archs = {"x86"}; break;     // EM_386
    case 40: h.archs = {"arm"}; break;    // EM_ARM
    default: h.archs = {"other"}; break;
    }
    return h;
}

// PE: the DOS header's e_lfanew leads to "PE\0\0" and the COFF header's Machine and Characteristics.
BinaryHeader pe(std::span<const std::byte> b) {
    BinaryHeader h;
    ByteReader r(b);
    r.seek(0x3c);
    const auto at = r.le<std::uint32_t>();
    if (at > b.size() || b.size() - at < 24) return h;
    r.seek(at);
    if (r.le<std::uint32_t>() != 0x00004550u) return h; // "PE\0\0"
    const auto machine = r.le<std::uint16_t>();
    r.skip(16);
    const auto characteristics = r.le<std::uint16_t>();
    h.format = BinaryFormat::Pe;
    h.library = (characteristics & 0x2000u) != 0; // IMAGE_FILE_DLL
    switch (machine) {
    case 0x8664: h.archs = {"x64"}; break;   // IMAGE_FILE_MACHINE_AMD64
    case 0xaa64: h.archs = {"arm64"}; break; // IMAGE_FILE_MACHINE_ARM64
    case 0x014c: h.archs = {"x86"}; break;   // IMAGE_FILE_MACHINE_I386
    default: h.archs = {"other"}; break;
    }
    return h;
}

std::string macho_arch(std::uint32_t cputype) {
    switch (cputype) {
    case 0x01000007u: return "x64";   // CPU_TYPE_X86_64
    case 0x0100000cu: return "arm64"; // CPU_TYPE_ARM64
    case 7u: return "x86";
    case 12u: return "arm";
    default: return "other";
    }
}

// A thin Mach-O at `at`: its magic (64- or 32-bit, either byte order), cputype and filetype.
bool macho_thin(std::span<const std::byte> b, std::size_t at, std::string& arch, bool& library) {
    if (at > b.size() || b.size() - at < 16) return false;
    ByteReader r(b);
    r.seek(at);
    const auto m = r.le<std::uint32_t>();
    bool big = false;
    if (m == 0xfeedfacfu || m == 0xfeedfaceu) big = false;
    else if (m == 0xcffaedfeu || m == 0xcefaedfeu) big = true;
    else return false;
    arch = macho_arch(read<std::uint32_t>(r, big));
    r.skip(4); // cpusubtype
    const auto filetype = read<std::uint32_t>(r, big);
    library = filetype == 6 || filetype == 8; // MH_DYLIB, MH_BUNDLE
    return true;
}

} // namespace

BinaryHeader read_binary_header(std::span<const std::byte> b) {
    try {
        if (magic(b, {0x7f, 'E', 'L', 'F'}) && b.size() >= 20) return elf(b);
        if (magic(b, {'M', 'Z'}) && b.size() >= 0x40) return pe(b);
        if (magic(b, {0xca, 0xfe, 0xba, 0xbe}) && b.size() >= 8) {
            // A universal file: big-endian fat_header, then nfat_arch fat_arch records of 20 bytes. (Java class files
            // share the magic; their "count" is a version far above any real number of architectures.)
            ByteReader r(b);
            r.skip(4);
            const auto n = r.be<std::uint32_t>();
            if (n == 0 || n > 16) return {};
            BinaryHeader h;
            h.format = BinaryFormat::MachO;
            h.library = true;
            for (std::uint32_t i = 0; i < n; ++i) {
                r.skip(8); // cputype, cpusubtype: the slice's own header says them again
                const auto offset = r.be<std::uint32_t>();
                r.skip(8); // size, align
                std::string arch;
                bool library = false;
                if (!macho_thin(b, offset, arch, library)) return {};
                h.archs.push_back(arch);
                h.library = h.library && library;
            }
            return h;
        }
        std::string arch;
        bool library = false;
        if (macho_thin(b, 0, arch, library)) {
            BinaryHeader h;
            h.format = BinaryFormat::MachO;
            h.archs = {arch};
            h.library = library;
            return h;
        }
    } catch (const std::out_of_range&) {
    }
    return {};
}

bool binary_fits_platform(const BinaryHeader& h, std::string_view platform) {
    const std::size_t dash = platform.find('-');
    if (dash == std::string_view::npos || !h.library) return false;
    const std::string_view os = platform.substr(0, dash), arch = platform.substr(dash + 1);
    const BinaryFormat want = os == "linux" ? BinaryFormat::Elf
                              : os == "windows" ? BinaryFormat::Pe
                              : os == "macos" ? BinaryFormat::MachO
                                              : BinaryFormat::Unknown;
    return want != BinaryFormat::Unknown && h.format == want &&
           std::find(h.archs.begin(), h.archs.end(), arch) != h.archs.end();
}

std::string_view plugin_extension(std::string_view platform) {
    if (platform.starts_with("linux-")) return ".so";
    if (platform.starts_with("windows-")) return ".dll";
    if (platform.starts_with("macos-")) return ".dylib";
    return {};
}

} // namespace nmsc
