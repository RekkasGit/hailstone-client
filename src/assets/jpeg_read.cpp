#include "assets/jpeg_read.h"

#include <climits>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include <stb_image.h>

namespace nmsc::assets {

bool is_jpeg(std::span<const std::byte> data) {
    return data.size() >= 3 && data[0] == std::byte{0xff} && data[1] == std::byte{0xd8} && data[2] == std::byte{0xff};
}

std::optional<Image> decode_jpeg(std::span<const std::byte> data, std::string* error) {
    if (!is_jpeg(data) || data.size() > INT_MAX) {
        if (error) *error = "not a JPEG";
        return std::nullopt;
    }
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data.data()), static_cast<int>(data.size()),
                                            &w, &h, &channels, 4);
    if (!pixels) {
        if (error) *error = std::string("JPEG: ") + stbi_failure_reason();
        return std::nullopt;
    }
    Image image;
    image.width = static_cast<std::uint32_t>(w);
    image.height = static_cast<std::uint32_t>(h);
    image.format = PixelFormat::RGBA8;
    image.mips.emplace_back(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    std::memcpy(image.mips[0].data(), pixels, image.mips[0].size());
    stbi_image_free(pixels);
    return image;
}

} // namespace nmsc::assets
