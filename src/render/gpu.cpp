#include "render/gpu.h"

#include <algorithm>
#include <bit>
#include <cstring>

#include "assets/texture.h"
#include "render/vk_context.h"
#include "render/vk_util.h"

namespace hailstone::render {

namespace {

VkFormat vk_format(assets::PixelFormat f) {
    switch (f) {
    case assets::PixelFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
    case assets::PixelFormat::BC1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case assets::PixelFormat::BC2: return VK_FORMAT_BC2_UNORM_BLOCK;
    case assets::PixelFormat::BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
    case assets::PixelFormat::BC4: return VK_FORMAT_BC4_UNORM_BLOCK;
    case assets::PixelFormat::BC5: return VK_FORMAT_BC5_UNORM_BLOCK;
    case assets::PixelFormat::BC7: return VK_FORMAT_BC7_UNORM_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}

std::uint32_t full_mip_count(std::uint32_t w, std::uint32_t h) {
    return static_cast<std::uint32_t>(std::bit_width(std::max(w, h)));
}

} // namespace

void Buffer::destroy(VmaAllocator allocator) {
    if (buffer) vmaDestroyBuffer(allocator, buffer, allocation);
    buffer = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
}

void Texture::destroy(VkDevice device, VmaAllocator allocator) {
    if (view) vkDestroyImageView(device, view, nullptr);
    if (image) vmaDestroyImage(allocator, image, allocation);
    view = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
}

UploadBatch::~UploadBatch() {
    // Abandoned uploads: free staging memory; targets were never handed out valid.
    for (auto& b : buffers_) b.staging.destroy(vk_.allocator());
    for (auto& t : textures_) t.staging.destroy(vk_.allocator());
}

Buffer UploadBatch::staging(std::span<const std::byte> data) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = std::max<VkDeviceSize>(data.size(), 4);
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    Buffer b;
    VmaAllocationInfo out{};
    vk_check(vmaCreateBuffer(vk_.allocator(), &info, &ai, &b.buffer, &b.allocation, &out), "create staging buffer");
    b.size = info.size;
    std::memcpy(out.pMappedData, data.data(), data.size());
    vk_check(vmaFlushAllocation(vk_.allocator(), b.allocation, 0, VK_WHOLE_SIZE), "flush staging buffer");
    return b;
}

Buffer UploadBatch::buffer(std::span<const std::byte> data, VkBufferUsageFlags usage) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = std::max<VkDeviceSize>(data.size(), 4);
    info.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    Buffer target;
    vk_check(vmaCreateBuffer(vk_.allocator(), &info, &ai, &target.buffer, &target.allocation, nullptr), "create buffer");
    target.size = info.size;
    buffers_.push_back({staging(data), target});
    return target;
}

std::optional<assets::Image> prepare_texture_image(const assets::Image& source, TextureUse use) {
    if (use != TextureUse::Data && source.mips.size() == 1 && (source.width > 1 || source.height > 1)) {
        assets::Image converted = assets::to_rgba8(source);
        if (use == TextureUse::OpaqueColor) assets::make_opaque(converted);
        else assets::remove_color_key(converted);
        assets::build_color_mips(converted);
        return converted;
    }
    if (use == TextureUse::Color && source.mips.size() > 1 &&
        (source.format == assets::PixelFormat::BC2 || source.format == assets::PixelFormat::BC3 ||
         source.format == assets::PixelFormat::RGBA8)) {
        // A game DDS with alpha and its own mips: built again from level 0 when it is painted on a colour key, as
        // its stored mips carry the key into the edges. (A pack's BC7 has its key out already, in its .k variant.)
        assets::Image level0 = source;
        level0.mips.resize(1);
        level0 = assets::to_rgba8(level0);
        if (assets::remove_color_key(level0)) {
            assets::build_color_mips(level0);
            return level0;
        }
    }
    return std::nullopt;
}

void drop_large_mips(assets::Image& image, std::uint32_t max_size) {
    if (max_size == 0) return;
    while (image.mips.size() > 1 && std::max(image.width, image.height) > max_size) {
        image.mips.erase(image.mips.begin());
        image.width = std::max(1u, image.width / 2);
        image.height = std::max(1u, image.height / 2);
    }
}

Texture UploadBatch::texture(const assets::Image& source, TextureUse use) {
    const assets::Image* image = &source;
    assets::Image converted;
    if (auto prepared = prepare_texture_image(source, use)) {
        converted = std::move(*prepared);
        image = &converted;
    }
    if (image == &source && source.format != assets::PixelFormat::RGBA8 && !vk_.caps().bc_textures) {
        converted = assets::to_rgba8(source);
        image = &converted;
    }

    const bool generate = image->format == assets::PixelFormat::RGBA8 && image->mips.size() == 1;
    const std::uint32_t levels = generate ? full_mip_count(image->width, image->height)
                                          : static_cast<std::uint32_t>(image->mips.size());
    const VkFormat format = vk_format(image->format);

    std::vector<std::byte> packed;
    std::vector<VkBufferImageCopy> regions;
    std::uint32_t w = image->width, h = image->height;
    for (std::uint32_t level = 0; level < image->mips.size(); ++level) {
        VkBufferImageCopy region{};
        region.bufferOffset = packed.size();
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        region.imageExtent = {w, h, 1};
        regions.push_back(region);
        packed.insert(packed.end(), image->mips[level].begin(), image->mips[level].end());
        // Keep offsets 16-byte aligned (a multiple of any texel block size).
        packed.resize((packed.size() + 15) & ~std::size_t{15});
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {image->width, image->height, 1};
    info.mipLevels = levels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 (generate ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    Texture t;
    vk_check(vmaCreateImage(vk_.allocator(), &info, &ai, &t.image, &t.allocation, nullptr), "create texture image");
    t.view = create_image_view(vk_.device(), t.image, format, VK_IMAGE_ASPECT_COLOR_BIT, levels);
    t.width = image->width;
    t.height = image->height;

    textures_.push_back({staging(packed), t.image, std::move(regions), levels, image->width, image->height, generate});
    return t;
}

void UploadBatch::submit() {
    if (buffers_.empty() && textures_.empty()) return;
    vk_.immediate_submit([&](VkCommandBuffer cmd) {
        for (auto& b : buffers_) {
            VkBufferCopy copy{0, 0, b.target.size};
            vkCmdCopyBuffer(cmd, b.staging.buffer, b.target.buffer, 1, &copy);
        }
        for (auto& t : textures_) {
            image_barrier(cmd, t.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                          VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            vkCmdCopyBufferToImage(cmd, t.staging.buffer, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   static_cast<std::uint32_t>(t.regions.size()), t.regions.data());
            if (t.generate_mips) {
                std::int32_t w = static_cast<std::int32_t>(t.width), h = static_cast<std::int32_t>(t.height);
                for (std::uint32_t level = 1; level < t.mip_levels; ++level) {
                    image_barrier(cmd, t.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
                                  VK_ACCESS_2_TRANSFER_READ_BIT, level - 1, 1);
                    std::int32_t nw = std::max(1, w / 2), nh = std::max(1, h / 2);
                    VkImageBlit blit{};
                    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
                    blit.srcOffsets[1] = {w, h, 1};
                    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
                    blit.dstOffsets[1] = {nw, nh, 1};
                    vkCmdBlitImage(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                    image_barrier(cmd, t.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                                  VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, level - 1, 1);
                    w = nw;
                    h = nh;
                }
                image_barrier(cmd, t.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                              VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, t.mip_levels - 1, 1);
            } else {
                image_barrier(cmd, t.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                              VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            }
        }
        VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        mb.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        // The buffers are read as vertices and indices, and by shaders as storage (the zone's light grid in
        // zone.frag) or uniform buffers. (No compute pipelines exist; add COMPUTE_SHADER here if one ever reads them.)
        mb.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                          VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        mb.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT |
                           VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(cmd, &dep);
    });
    for (auto& b : buffers_) b.staging.destroy(vk_.allocator());
    for (auto& t : textures_) t.staging.destroy(vk_.allocator());
    buffers_.clear();
    textures_.clear();
}

} // namespace hailstone::render
