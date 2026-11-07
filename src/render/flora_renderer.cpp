#include "render/flora_renderer.h"

#include <algorithm>
#include <chrono>
#include <span>
#include <cmath>
#include <fstream>
#include <stdexcept>

#include "assets/texture.h"
#include "assets/texture_overrides.h"
#include "assets/zone.h"
#include "assets/zone_flora.h"
#include "core/log.h"
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

// One plant as the vertex shader takes it, per instance (40 bytes: the light and haze are the shaders').
struct GpuPlant {
    glm::vec4 position_turn; // xyz the foot, w its turn (radians)
    glm::vec4 size;          // x width, y height, z the sway's reach (fraction of the height), w its period (s; 0 none)
    std::uint32_t color = 0x80808080u; // the ground's colour (RGBA8) for *INHERITVERTEXCOLOR, 0x80 neutral
    std::uint32_t flags = 0;           // bits 0..7 the texture slot, 8..10 the quarter (4 the whole), 11 crossed
};
static_assert(sizeof(GpuPlant) == 40);

// flora.vert / flora.frag's Frame buffer.
struct FloraFrame {
    glm::vec4 classic{0.0f}; // rgb the classic lighting's one light (linear), w 1 modern
    glm::vec4 params{0.0f};  // x the pushers
    std::array<glm::vec4, FloraRenderer::kMaxPushers> pushers{};
};

constexpr std::uint32_t kSets = VkContext::kFramesInFlight;

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

float flora_keep(float d) {
    if (d <= kFloraReach) return 1.0f;
    const float r = kFloraReach / d;
    return r * r;
}

void FloraRenderer::set_reach(float reach) { reach_ = std::clamp(reach, kFloraReach, kMaxFloraReach); }

void FloraRenderer::set_pushers(std::span<const glm::vec4> pushers) {
    pushers_.assign(pushers.begin(), pushers.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(pushers.size(), kMaxPushers)));
}

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
                std::optional<std::vector<std::byte>> data;
                for (const auto& donor : zone_.flora.donors)
                    if ((data = donor->read(def.texture))) break;
                if (zone_.flora.donors.empty()) data = zone_.read_file(def.texture);
                std::optional<std::span<const std::byte>> bytes;
                if (data) bytes = std::span<const std::byte>(*data);
                if (auto image = assets::decode_game_texture(def.texture, bytes, {.reads_alpha = true})) {
                    slot = static_cast<std::uint32_t>(textures_.size());
                    textures_.push_back(upload.texture(*image, TextureUse::Color));
                } else {
                    log::debug("flora texture {} not found", def.texture);
                    // Hailstone's zone flora leaves out a donor's plant without its picture (elddar.eqg's flowers
                    // name a texture it does not hold) rather than drawing it white; an EQG zone's own keeps slot 0.
                    if (!zone_.flora.donors.empty()) slot = kNoTexture;
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

    // 0 the lighting (render/scene_lighting.h), 1 the frame's FloraFrame, 2 the textures; per frame in flight.
    VkDescriptorSetLayoutBinding bindings[3]{};
    for (std::uint32_t k = 0; k < 2; ++k) {
        bindings[k].binding = k;
        bindings[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[k].descriptorCount = 1;
        bindings[k].stageFlags = kPushStages;
    }
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = max_textures_;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorBindingFlags flags[3] = {0, 0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | vk_.texture_array_binding_flags()};
    VkDescriptorSetLayoutBindingFlagsCreateInfo bf{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    bf.bindingCount = 3;
    bf.pBindingFlags = flags;
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.pNext = &bf;
    li.flags = vk_.texture_array_layout_flags();
    li.bindingCount = 3;
    li.pBindings = bindings;
    vk_check(vkCreateDescriptorSetLayout(d, &li, nullptr, &set_layout_), "vkCreateDescriptorSetLayout (flora)");
    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * kSets},
                                     {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_textures_ * kSets}};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.flags = vk_.texture_array_pool_flags();
    pi.maxSets = kSets;
    pi.poolSizeCount = 2;
    pi.pPoolSizes = sizes;
    vk_check(vkCreateDescriptorPool(d, &pi, nullptr, &pool_), "vkCreateDescriptorPool (flora)");
    lighting_buffers_ = std::make_unique<FrameBuffers>(vk_, sizeof(SceneLighting), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    frame_buffers_ = std::make_unique<FrameBuffers>(vk_, sizeof(FloraFrame), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    std::vector<VkDescriptorImageInfo> infos;
    for (const auto& t : textures_) infos.push_back({sampler_, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    for (std::uint32_t i = 0; i < kSets; ++i) {
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &set_layout_;
        vk_check(vkAllocateDescriptorSets(d, &ai, &sets_[i]), "vkAllocateDescriptorSets (flora)");
        VkDescriptorBufferInfo buffers[2] = {{lighting_buffers_->buffer(i), 0, VK_WHOLE_SIZE},
                                             {frame_buffers_->buffer(i), 0, VK_WHOLE_SIZE}};
        VkWriteDescriptorSet w[3]{};
        for (std::uint32_t k = 0; k < 3; ++k) {
            w[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[k].dstSet = sets_[i];
            w[k].dstBinding = k;
        }
        for (std::uint32_t k = 0; k < 2; ++k) {
            w[k].descriptorCount = 1;
            w[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[k].pBufferInfo = &buffers[k];
        }
        w[2].descriptorCount = static_cast<std::uint32_t>(infos.size());
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[2].pImageInfo = infos.data();
        vkUpdateDescriptorSets(d, 3, w, 0, nullptr);
    }

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

void FloraRenderer::grow_tiles(const glm::vec3& camera) {
    const auto& flora = zone_.flora;
    // Let go of the tiles well out of reach (a tile's width past it), so the memory follows the reach.
    const float keep = reach_ + flora.tile_size * 2.0f;
    const glm::vec2 c(camera);
    for (auto it = grown_.begin(); it != grown_.end();) {
        const auto& tile = flora.tiles[it->first];
        glm::vec2 nearest = glm::clamp(c, tile.min, tile.min + glm::vec2(flora.tile_size));
        if (glm::dot(nearest - c, nearest - c) > keep * keep) it = grown_.erase(it);
        else ++it;
    }
    // Grow the missing ones nearest first, for at most kGrowBudget a frame (a zone-in grows its first ring over a few
    // frames; walking grows a tile now and then).
    const auto start = std::chrono::steady_clock::now();
    for (const auto& [d2, t] : near_tiles_) {
        if (grown_.contains(t)) continue;
        std::vector<assets::FloraInstance> plants;
        flora.grower->grow(t, plants);
        grown_.emplace(t, std::move(plants));
        if (std::chrono::steady_clock::now() - start > kGrowBudget) break;
    }
}

std::uint32_t FloraRenderer::draw(const FrameInfo& frame, const glm::mat4& view_proj, const glm::vec3& camera,
                                  double seconds, const SceneLighting& lighting, bool modern) {
    const auto& flora = zone_.flora;
    if (flora.empty() || !pipeline_) return 0;
    auto* out = static_cast<GpuPlant*>(mapped_[frame.frame_slot]);
    std::uint32_t count = 0;
    const float reach = reach_;
    const float reach2 = reach * reach;
    // The tiles within reach of the camera (in the plane), nearest first, so the plants near the camera are never the
    // ones kMaxPlants leaves out.
    near_tiles_.clear();
    const glm::vec2 c(camera);
    for (std::uint32_t t = 0; t < flora.tiles.size(); ++t) {
        const auto& tile = flora.tiles[t];
        glm::vec2 nearest = glm::clamp(c, tile.min, tile.min + glm::vec2(flora.tile_size));
        float d2 = glm::dot(nearest - c, nearest - c);
        if (d2 <= reach2) near_tiles_.emplace_back(d2, t);
    }
    std::sort(near_tiles_.begin(), near_tiles_.end());
    if (flora.grower) grow_tiles(camera);
    for (const auto& [tile_d2, t] : near_tiles_) {
        const auto& tile = flora.tiles[t];
        if (count >= kMaxPlants) break;
        // A grown zone's tile (assets::ZoneFloraGrower), or the zone's own plants.
        std::span<const assets::FloraInstance> plants;
        if (flora.grower) {
            auto it = grown_.find(t);
            if (it == grown_.end()) continue; // not grown yet: next frame
            plants = it->second;
        } else {
            plants = std::span<const assets::FloraInstance>(flora.instances).subspan(tile.first, tile.count);
        }
        for (std::uint32_t j = 0; j < plants.size() && count < kMaxPlants; ++j) {
            const auto& p = plants[j];
            const std::uint32_t i = flora.grower ? t * 65537u + j : tile.first + j; // the plant's own key
            glm::vec3 d = p.position - camera;
            float dist2 = glm::dot(d, d);
            if (dist2 > reach2) continue;
            // Past kFloraReach, a share of the plants by distance, the same ones from frame to frame (by index).
            if (dist2 > kFloraReach * kFloraReach &&
                static_cast<float>((i * 2654435761u) >> 8 & 0xffffu) / 65536.0f >= flora_keep(std::sqrt(dist2)))
                continue;
            // Behind the camera, out of reach of the view.
            glm::vec4 clip = view_proj * glm::vec4(p.position + glm::vec3(0.0f, 0.0f, p.height * 0.5f), 1.0f);
            if (clip.w < -std::max(p.width, p.height)) continue;
            const auto& def = flora.definitions[p.definition];
            // Into the ground over the last fifth of the reach.
            float fade = std::clamp((reach - std::sqrt(dist2)) / (reach * 0.2f), 0.0f, 1.0f);
            if (fade <= 0.0f) continue;
            std::uint32_t quarter = 4;
            if (def.quad_texture) quarter = static_cast<std::uint32_t>(std::clamp(def.texture_index, 0, 3));
            GpuPlant g;
            g.position_turn = glm::vec4(p.position, glm::radians(p.rotation));
            g.size = glm::vec4(p.width * fade, p.height * fade, def.sway ? def.sway_displacement : 0.0f,
                               def.sway ? std::max(def.sway_period, 0.1f) : 0.0f);
            if (def.inherit_vertex_color) g.color = p.color; // the terrain's colour (RGBA bytes), 0x80 the neutral grey
            const std::uint32_t slot = p.definition < definition_slot_.size() ? definition_slot_[p.definition] : 0;
            if (slot == kNoTexture) continue;
            g.flags = (slot & 0xffu) | quarter << 8 | (def.cross ? 0x800u : 0u);
            out[count++] = g;
        }
    }
    if (count == 0) return 0;
    vmaFlushAllocation(vk_.allocator(), instances_[frame.frame_slot].allocation, 0, sizeof(GpuPlant) * count);
    // The light is the shaders' (flora.vert, flora.frag): the frame's lighting as the zone took it, and for the classic
    // lighting the scene's ambient and half its sun (display values), the same for every plant.
    lighting_buffers_->write(frame.frame_slot, &lighting, sizeof(SceneLighting));
    FloraFrame ff;
    ff.classic = glm::vec4(
        to_linear(glm::min(glm::vec3(lighting.ambient) + glm::vec3(lighting.directional) * 0.5f, glm::vec3(1.0f))),
        modern ? 1.0f : 0.0f);
    ff.params.x = static_cast<float>(pushers_.size());
    std::copy(pushers_.begin(), pushers_.end(), ff.pushers.begin());
    frame_buffers_->write(frame.frame_slot, &ff, sizeof(ff));
    VkCommandBuffer cmd = frame.cmd;
    Push pc{view_proj, glm::vec4(static_cast<float>(std::fmod(seconds, 4096.0)), 0.0f, 0.0f, 0.0f)};
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkDescriptorSet sets[2] = {sets_[frame.frame_slot], vk_.shadow_set()};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 2, sets, 0, nullptr);
    vkCmdBindVertexBuffers(cmd, 0, 1, &instances_[frame.frame_slot].buffer, &offset);
    vkCmdPushConstants(cmd, layout_, kPushStages, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 12, count, 0, 0);
    return count;
}

void FloraRenderer::create_pipeline(const std::filesystem::path& shader_dir) {
    VkDevice d = vk_.device();
    VkPushConstantRange range{kPushStages, 0, sizeof(Push)};
    VkDescriptorSetLayout set_layouts[2] = {set_layout_, vk_.shadow_set_layout()};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 2;
    li.pSetLayouts = set_layouts;
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
        {2, 0, VK_FORMAT_R32_UINT, offsetof(GpuPlant, color)},
        {3, 0, VK_FORMAT_R32_UINT, offsetof(GpuPlant, flags)},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 4;
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
