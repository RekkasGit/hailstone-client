#include "render/spirv_file.h"

#include <fstream>
#include <stdexcept>

namespace hailstone::render {

std::vector<std::uint32_t> read_spirv_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open shader " + path.string());
    const auto end = f.tellg();
    if (end <= 0 || end % 4 != 0) throw std::runtime_error("bad SPIR-V size in " + path.string());
    std::vector<std::uint32_t> code(static_cast<std::size_t>(end) / 4);
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(code.size() * 4)))
        throw std::runtime_error("cannot read shader " + path.string());
    return code;
}

} // namespace hailstone::render
