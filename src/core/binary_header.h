#pragma once

// What kind of program file a shared library or a program is, read from its header alone (the add-on manager,
// docs/addons.md): an add-on's compiled plugin is checked to be a library for this platform, and a tool's program to
// be a program for it, before it is installed, without loading or running it. ELF (Linux), PE (Windows) and Mach-O (macOS, a universal file too). The bytes come from
// a download: every offset is checked, and anything that does not read is "unknown".

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hailstone {

enum class BinaryFormat { Unknown, Elf, Pe, MachO };

struct BinaryHeader {
    BinaryFormat format = BinaryFormat::Unknown;
    std::vector<std::string> archs; // "x64", "arm64", "x86", "arm", or "other"; several in a universal Mach-O file
    bool library = false;           // a shared library (ELF ET_DYN, a PE DLL, a Mach-O dylib or bundle)
    bool program = false;           // a program (ELF ET_EXEC or ET_DYN, as a position-independent one is; a PE
                                    // executable image that is not a DLL; a Mach-O MH_EXECUTE)
};

BinaryHeader read_binary_header(std::span<const std::byte> bytes);

// Whether the file is a library this platform (update_platform()'s names: "linux-x64", "windows-arm64",
// "macos-arm64" ...) can load.
bool binary_fits_platform(const BinaryHeader& header, std::string_view platform);

// Whether the file is a program this platform can run (the same names).
bool program_fits_platform(const BinaryHeader& header, std::string_view platform);

// The library file extension of a platform's plugins (".so", ".dll", ".dylib"; "" unknown).
std::string_view plugin_extension(std::string_view platform);

} // namespace hailstone
