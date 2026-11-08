#pragma once

// A compiled shader (.spv) read from the client's shader folder, for vkCreateShaderModule. Kept apart from Vulkan so
// the checks are tested.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace hailstone::render {

// The file's 32-bit words. Throws std::runtime_error when it cannot be opened or read, or when its size is 0 or not
// a multiple of 4 (a truncated or damaged file: SPIR-V is a stream of words).
std::vector<std::uint32_t> read_spirv_file(const std::filesystem::path& path);

} // namespace hailstone::render
