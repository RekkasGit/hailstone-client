#include "render/flora_renderer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

#include "assets/texture.h"
#include "assets/texture_overrides.h"
#include "assets/zone.h"
#include "core/log.h"
#include "render/particle_renderer.h"
#include "render/scene_lighting.h"
#include "render/vk_util.h"

namespace nmsc::render {

namespace {

constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// flora.vert / flora.frag's push block.
struct Push {
    glm::mat4 view_proj;
    glm::vec4 time; // x seconds (wrapped)
};
static_assert(sizeof(Push) <= 128);

// One plant as the vertex shader takes it, per instance.
struct GpuPlant {
    glm::vec4 position_turn; // xyz the foot, w its turn (radians)
    glm::vec4 size;          // x width, y height, z the sway's reach (fraction of the height), w its period (s; 0 none)
    glm::vec4 uv;            // the picture's corner and size in the texture
    glm::vec4 shade;         // rgb the light (linear, with the terrain's colour), w 1 for two crossed quads
    glm::vec4 haze;          // rgb the haze in front of it (linear), w how much of it shows through
    std::uint32_t texture = 0;
};

VkShaderModule load_shader(VkDevice device, const std::filesystem::path& path) {
    return create_shader_module(device, path);
}

glm::vec3 to_linear(const glm::vec3& c) {
    auto one = [](float v) {
        v = std::max(v, 0.0f);
        return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    return {one(c.r), one(c.g), one(c.b)};
}

} // namespace

FloraRenderer::FloraRenderer(VkContext& vk, const assets::Zone& zone, const std::filesystem::path& shader_dir)
    : vk_(vk), zone_(zone) {
    VkDevice d = vk_.device();
    max_textures_ = std::min(kMaxTextures, vk_.caps().max_sampled_images);
    {
        UploadBatch upload(vk_);
        assets::Image white;
        white.width = white.height = 1;
        white.mips.push_back(std::vector<std::byte>(4, std::byte{0xff}));
        textures_.push_back(upload.texture(white));
        // Each definition's texture, once per name.
        std::vector<std::pair<std::string, std::uint32_t>> loaded;
        for (const auto& def : zone_.flora.definitions) {
            std::uint32_t slot = 0;
            auto it = std::find_if(loaded.begin(), loaded.end(), [&](const auto& l) { return l.first == def.texture; });
            if (it != loaded.end()) {
                slot = it->second;
            } else if (textures_.size() < max_textures_) {
                auto data = zone_.flora.textures ? zone_.flora.textures->read(def.texture) : zone_.read_file(def.texture);
                std::optional<std::span<const std::byte>> bytes;
                if (data) bytes = std::span<const std::byte>(*data);
                if (auto image = assets::decode_game_texture(def.texture, bytes, {.reads_alpha = true})) {
                    slot = static_cast<std::uint32_t>(textures_.size());
                    textures_.push_back(upload.texture(*image, TextureUse::Color));
                } else {
                    log::debug("flora texture {} not found", def.texture);
                }
                loaded.emplace_back(def.texture, slot);
            }
            definition_slot_.push_back(slot);
        }
        upload.submit();
    }
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = VK_LOD_CLAMP_NONE;
    vk_check(vkCreateSampler(d, &si, nullptr, &sampler_), "vkCreateSampler (flora)");

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = max_textures_;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | vk_.texture_array_binding_flags();
    VkDescriptorSetLayoutBindingFlagsCreateInfo bf{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    bf.bindingCount = 1;
    bf.pBindingFlags = &flags;
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.pNext = &bf;
    li.flags = vk_.texture_array_layout_flags();
    li.bindingCount = 1;
    li.pBindings = &binding;
    vk_check(vkCreateDescriptorSetLayout(d, &li, nullptr, &set_layout_), "vkCreateDescriptorSetLayout (flora)");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_textures_};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.flags = vk_.texture_array_pool_flags();
    pi.maxSets = 1;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    vk_check(vkCreateDescriptorPool(d, &pi, nullptr, &pool_), "vkCreateDescriptorPool (flora)");
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &set_layout_;
    vk_check(vkAllocateDescriptorSets(d, &ai, &set_), "vkAllocateDescriptorSets (flora)");
    std::vector<VkDescriptorImageInfo> infos;
    for (const auto& t : textures_) infos.push_back({sampler_, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set_;
    w.dstBinding = 0;
    w.descriptorCount = static_cast<std::uint32_t>(infos.size());
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = infos.data();
    vkUpdateDescriptorSets(d, 1, &w, 0, nullptr);

    for (std::size_t i = 0; i < instances_.size(); ++i) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = sizeof(GpuPlant) * kMaxPlants;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo out{};
        vk_check(vmaCreateBuffer(vk_.allocator(), &bi, &aci, &instances_[i].buffer, &instances_[i].allocation, &out),
                 "create flora instance buffer");
        instances_[i].size = bi.size;
        mapped_[i] = out.pMappedData;
    }
    create_pipeline(shader_dir);
}

FloraRenderer::~FloraRenderer() {
    VkDevice d = vk_.device();
    vk_.wait_idle();
    if (pipeline_) vkDestroyPipeline(d, pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(d, layout_, nullptr);
    if (pool_) vkDestroyDescriptorPool(d, pool_, nullptr);
    if (set_layout_) vkDestroyDescriptorSetLayout(d, set_layout_, nullptr);
    if (sampler_) vkDestroySampler(d, sampler_, nullptr);
    for (auto& t : textures_) t.destroy(d, vk_.allocator());
    for (auto& b : instances_) b.destroy(vk_.allocator());
}

std::uint32_t FloraRenderer::draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera,
                                  double seconds, const SceneLighting& lighting, bool modern) {
    const auto& flora = zone_.flora;
    if (flora.empty() || !pipeline_) return 0;
    auto* out = static_cast<GpuPlant*>(mapped_[frame.frame_slot]);
    std::uint32_t count = 0;
    const float reach2 = kFloraReach * kFloraReach;
    // The classic lighting: the scene's ambient and half its sun (display values), the same for every plant.
    const glm::vec3 classic_light =
        to_linear(glm::min(glm::vec3(lighting.ambient) + glm::vec3(lighting.directional) * 0.5f, glm::vec3(1.0f)));
    const ParticleLight modern_light(lighting); // the modern light, its frame's part worked out once
    for (const auto& tile : flora.tiles) {
        // The tile's square within reach of the camera (in the plane).
        glm::vec2 c(camera);
        glm::vec2 nearest = glm::clamp(c, tile.min, tile.min + glm::vec2(flora.tile_size));
        if (glm::dot(nearest - c, nearest - c) > reach2) continue;
        for (std::uint32_t i = tile.first; i < tile.first + tile.count && count < kMaxPlants; ++i) {
            const auto& p = flora.instances[i];
            glm::vec3 d = p.position - camera;
            float dist2 = glm::dot(d, d);
            if (dist2 > reach2) continue;
            // Behind the camera, out of reach of the view.
            glm::vec4 clip = view_proj * glm::vec4(p.position + glm::vec3(0.0f, 0.0f, p.height * 0.5f), 1.0f);
            if (clip.w < -std::max(p.width, p.height)) continue;
            const auto& def = flora.definitions[p.definition];
            // Into the ground over the last fifth of the reach.
            float fade = std::clamp((kFloraReach - std::sqrt(dist2)) / (kFloraReach * 0.2f), 0.0f, 1.0f);
            if (fade <= 0.0f) continue;
            glm::vec4 uv(0.0f, 0.0f, 1.0f, 1.0f);
            if (def.quad_texture) {
                int q = std::clamp(def.texture_index, 0, 3);
                uv = glm::vec4(static_cast<float>(q % 2) * 0.5f, static_cast<float>(q / 2) * 0.5f, 0.5f, 0.5f);
            }
            glm::vec3 tint(1.0f);
            if (def.inherit_vertex_color) // the terrain's colour (RGBA bytes), 0x80 the neutral grey
                tint = glm::vec3(static_cast<float>(p.color & 0xff), static_cast<float>(p.color >> 8 & 0xff),
                                 static_cast<float>(p.color >> 16 & 0xff)) / 128.0f;
            glm::vec3 light = classic_light;
            glm::vec4 haze(0.0f, 0.0f, 0.0f, 1.0f);
            if (modern) {
                ParticleHaze h = haze_at(lighting, p.position);
                light = modern_light.at(p.position + glm::vec3(0.0f, 0.0f, p.height * 0.5f)) * h.absorb;
                haze = glm::vec4(h.color, h.through);
            }
            GpuPlant g;
            g.position_turn = glm::vec4(p.position, glm::radians(p.rotation));
            g.size = glm::vec4(p.width * fade, p.height * fade, def.sway ? def.sway_displacement : 0.0f,
                               def.sway ? std::max(def.sway_period, 0.1f) : 0.0f);
            g.uv = uv;
            g.shade = glm::vec4(light * tint, def.cross ? 1.0f : 0.0f);
            g.haze = haze;
            g.texture = p.definition < definition_slot_.size() ? definition_slot_[p.definition] : 0;
            out[count++] = g;
        }
    }
    if (count == 0) return 0;
    vmaFlushAllocation(vk_.allocator(), instances_[frame.frame_slot].allocation, 0, sizeof(GpuPlant) * count);
    VkCommandBuffer cmd = frame.cmd;
    Push pc{view_proj, glm::vec4(static_cast<float>(std::fmod(seconds, 4096.0)), 0.0f, 0.0f, 0.0f)};
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set_, 0, nullptr);
    vkCmdBindVertexBuffers(cmd, 0, 1, &instances_[frame.frame_slot].buffer, &offset);
    vkCmdPushConstants(cmd, layout_, kPushStages, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 12, count, 0, 0);
    return count;
}

void FloraRenderer::create_pipeline(const std::filesystem::path& shader_dir) {
    VkDevice d = vk_.device();
    VkPushConstantRange range{kPushStages, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &set_layout_;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &range;
    vk_check(vkCreatePipelineLayout(d, &li, nullptr, &layout_), "vkCreatePipelineLayout (flora)");
    VkShaderModule vs = load_shader(d, shader_dir / "flora.vert.spv");
    VkShaderModule fs = load_shader(d, shader_dir / "flora.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr},
    };
    VkVertexInputBindingDescription binding{0, sizeof(GpuPlant), VK_VERTEX_INPUT_RATE_INSTANCE};
    VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuPlant, position_turn)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuPlant, size)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuPlant, uv)},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuPlant, shade)},
        {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GpuPlant, haze)},
        {5, 0, VK_FORMAT_R32_UINT, offsetof(GpuPlant, texture)},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 6;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = vk_.scene_samples();
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    dss.depthTestEnable = VK_TRUE;
    dss.depthWriteEnable = VK_TRUE;
    dss.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; // reverse Z
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &ba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkFormat color_format = vk_.scene_format();
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &color_format;
    ri.depthAttachmentFormat = vk_.depth_format();
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &ri;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &dss;
    info.pColorBlendState = &cb;
    info.pDynamicState = &ds;
    info.layout = layout_;
    vk_check(vkCreateGraphicsPipelines(d, vk_.pipeline_cache(), 1, &info, nullptr, &pipeline_),
             "vkCreateGraphicsPipelines (flora)");
    vkDestroyShaderModule(d, vs, nullptr);
    vkDestroyShaderModule(d, fs, nullptr);
}

} // namespace nmsc::render
