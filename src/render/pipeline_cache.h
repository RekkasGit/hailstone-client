#pragma once

// The Vulkan pipeline cache kept between runs in cache/pipelines.bin (platform::pipeline_cache_file()), so the
// driver need not compile the same pipelines again at each start.
//
// The file is Hailstone's header and then the driver's data (vkGetPipelineCacheData):
//   "HSPC", u32 format (1), u32 driver version, u32 0, u64 data size, u64 FNV-1a hash of the data   (32 bytes, LE)
// The driver's data starts with VkPipelineCacheHeaderVersionOne: u32 header length (32 or more), u32 header
// version (1), u32 vendor id, u32 device id, u8 pipeline cache UUID[16]. A file that is cut short, whose hash does
// not match, or that another GPU or driver wrote is not given to the driver (some drivers trust it): the cache
// starts empty and the file is written again at the end of the run.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace nmsc::render {

// What a cache must have been made by (VkPhysicalDeviceProperties).
struct PipelineCacheDevice {
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t driver_version = 0;
    std::array<std::uint8_t, 16> uuid{}; // pipelineCacheUUID
};

inline constexpr std::size_t kPipelineCacheFileHeader = 32;

// The file for the driver's `data`.
std::vector<std::byte> pack_pipeline_cache(std::span<const std::byte> data, const PipelineCacheDevice& device);
// The driver's data in `file` when the file is whole and `device` made it; else nullopt, with the reason in `why`.
std::optional<std::vector<std::byte>> unpack_pipeline_cache(std::span<const std::byte> file,
                                                            const PipelineCacheDevice& device,
                                                            std::string* why = nullptr);

// Reads `path` for `device`: the driver's data, or empty (no file, or one not used; the log says why).
std::vector<std::byte> load_pipeline_cache(const std::filesystem::path& path, const PipelineCacheDevice& device);
// Writes `data` to `path` through a temporary file (core/atomic_file.h); false when that fails.
bool save_pipeline_cache(const std::filesystem::path& path, std::span<const std::byte> data,
                         const PipelineCacheDevice& device);

} // namespace nmsc::render
