#include "render/gpu_requirements.h"

#include <algorithm>

namespace nmsc::render {

std::vector<std::string> gpu_missing(const GpuSupport& gpu) {
    std::vector<std::string> missing;
    if (gpu.api_major < 1 || (gpu.api_major == 1 && gpu.api_minor < 3)) {
        missing.push_back("Vulkan 1.3 (its driver offers " + std::to_string(gpu.api_major) + "." +
                          std::to_string(gpu.api_minor) + ")");
        return missing;
    }
    if (!gpu.dynamic_rendering) missing.emplace_back("dynamic rendering");
    if (!gpu.synchronization2) missing.emplace_back("synchronization2");
    if (!gpu.runtime_descriptor_array) missing.emplace_back("runtime descriptor arrays");
    if (!gpu.nonuniform_sampled_image_indexing) missing.emplace_back("non-uniform sampled image indexing");
    if (!gpu.descriptor_binding_partially_bound) missing.emplace_back("partially bound descriptors");
    if (!gpu.descriptor_binding_variable_count) missing.emplace_back("variable descriptor counts");
    if (!gpu.separate_depth_stencil_layouts) missing.emplace_back("separate depth and stencil layouts");
    if (!gpu.presents) missing.emplace_back("drawing to this window");
    return missing;
}

std::string no_usable_gpu_text(const std::vector<GpuSupport>& gpus) {
    if (gpus.empty()) return "no usable GPU: Vulkan found no GPU at all";
    std::string text = "no usable GPU:";
    for (const GpuSupport& gpu : gpus) {
        const std::vector<std::string> missing = gpu_missing(gpu);
        text += "\n- " + (gpu.name.empty() ? std::string("unnamed GPU") : gpu.name) + ": ";
        if (missing.empty()) {
            text += "usable";
            continue;
        }
        text += "lacks ";
        for (std::size_t i = 0; i < missing.size(); ++i) {
            if (i) text += ", ";
            text += missing[i];
        }
    }
    return text;
}

InstanceExtensions choose_instance_extensions(const std::vector<std::string>& wanted,
                                              const std::vector<std::string>& offered) {
    static const std::string kPortability = "VK_KHR_portability_enumeration";
    auto has = [](const std::vector<std::string>& list, const std::string& name) {
        return std::find(list.begin(), list.end(), name) != list.end();
    };
    InstanceExtensions out;
    for (const std::string& name : wanted) {
        if (name == kPortability || has(out.enable, name) || has(out.missing, name)) continue;
        (has(offered, name) ? out.enable : out.missing).push_back(name);
    }
    if (has(offered, kPortability)) {
        out.enable.push_back(kPortability);
        out.portability = true;
    }
    return out;
}

} // namespace nmsc::render
