#include "render/pipeline_cache.h"

#include <cstring>
#include <string_view>

#include "core/atomic_file.h"
#include "core/game_dir.h"
#include "core/log.h"

namespace nmsc::render {

namespace {
constexpr char kMagic[4] = {'H', 'S', 'P', 'C'};
constexpr std::uint32_t kFormat = 1;
constexpr std::size_t kVulkanHeader = 32; // VkPipelineCacheHeaderVersionOne

std::uint64_t fnv1a(std::span<const std::byte> data) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::byte b : data) {
        h ^= static_cast<std::uint8_t>(b);
        h *= 0x100000001b3ull;
    }
    return h;
}

void put32(std::vector<std::byte>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::byte>(v >> (8 * i)));
}
void put64(std::vector<std::byte>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::byte>(v >> (8 * i)));
}
std::uint32_t get32(std::span<const std::byte> in, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(in[at + i]) << (8 * i);
    return v;
}
std::uint64_t get64(std::span<const std::byte> in, std::size_t at) {
    return get32(in, at) | static_cast<std::uint64_t>(get32(in, at + 4)) << 32;
}

std::optional<std::vector<std::byte>> refuse(std::string* why, const char* reason) {
    if (why) *why = reason;
    return std::nullopt;
}
} // namespace

std::vector<std::byte> pack_pipeline_cache(std::span<const std::byte> data, const PipelineCacheDevice& device) {
    std::vector<std::byte> out;
    out.reserve(kPipelineCacheFileHeader + data.size());
    for (char c : kMagic) out.push_back(static_cast<std::byte>(c));
    put32(out, kFormat);
    put32(out, device.driver_version);
    put32(out, 0);
    put64(out, data.size());
    put64(out, fnv1a(data));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

std::optional<std::vector<std::byte>> unpack_pipeline_cache(std::span<const std::byte> file,
                                                            const PipelineCacheDevice& device, std::string* why) {
    if (file.size() < kPipelineCacheFileHeader || std::memcmp(file.data(), kMagic, 4) != 0)
        return refuse(why, "not a pipeline cache file");
    if (get32(file, 4) != kFormat) return refuse(why, "another format");
    if (get32(file, 8) != device.driver_version) return refuse(why, "made by another driver version");
    std::span<const std::byte> data = file.subspan(kPipelineCacheFileHeader);
    if (get64(file, 16) != data.size()) return refuse(why, "cut short");
    if (get64(file, 24) != fnv1a(data)) return refuse(why, "damaged (hash mismatch)");
    if (data.size() < kVulkanHeader || get32(data, 0) < kVulkanHeader || get32(data, 0) > data.size() ||
        get32(data, 4) != 1)
        return refuse(why, "no Vulkan cache header");
    if (get32(data, 8) != device.vendor_id || get32(data, 12) != device.device_id ||
        std::memcmp(data.data() + 16, device.uuid.data(), device.uuid.size()) != 0)
        return refuse(why, "made by another GPU or driver");
    return std::vector<std::byte>(data.begin(), data.end());
}

std::vector<std::byte> load_pipeline_cache(const std::filesystem::path& path, const PipelineCacheDevice& device) {
    auto file = read_file(path);
    if (!file) return {};
    std::string why;
    auto data = unpack_pipeline_cache(*file, device, &why);
    if (!data) {
        log::info("pipeline cache {}: {}; starting empty", path.string(), why);
        return {};
    }
    log::info("pipeline cache {}: {} bytes", path.string(), data->size());
    return std::move(*data);
}

bool save_pipeline_cache(const std::filesystem::path& path, std::span<const std::byte> data,
                         const PipelineCacheDevice& device) {
    std::vector<std::byte> file = pack_pipeline_cache(data, device);
    return write_file_atomic(path, std::string_view(reinterpret_cast<const char*>(file.data()), file.size()));
}

} // namespace nmsc::render
