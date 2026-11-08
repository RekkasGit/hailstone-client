// The dialog for an error that stops Hailstone (app/startup_error.h) and the GPU check behind its commonest case
// (render/gpu_requirements.h).

#include <doctest/doctest.h>

#include "app/startup_error.h"
#include "render/gpu_requirements.h"

using namespace hailstone;

namespace {

render::GpuSupport full_gpu(const char* name) {
    render::GpuSupport g;
    g.name = name;
    g.api_major = 1;
    g.api_minor = 3;
    g.dynamic_rendering = true;
    g.synchronization2 = true;
    g.runtime_descriptor_array = true;
    g.nonuniform_sampled_image_indexing = true;
    g.descriptor_binding_partially_bound = true;
    g.descriptor_binding_variable_count = true;
    g.separate_depth_stencil_layouts = true;
    g.presents = true;
    return g;
}

bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

} // namespace

TEST_CASE("gpu_missing: a GPU with everything lacks nothing") {
    CHECK(render::gpu_missing(full_gpu("A")).empty());
    render::GpuSupport newer = full_gpu("B");
    newer.api_minor = 4;
    CHECK(render::gpu_missing(newer).empty());
}

TEST_CASE("gpu_missing: below Vulkan 1.3 only the version is named") {
    render::GpuSupport g; // no features read from such a device
    g.api_major = 1;
    g.api_minor = 2;
    const auto missing = render::gpu_missing(g);
    REQUIRE(missing.size() == 1);
    CHECK(missing[0] == "Vulkan 1.3 (its driver offers 1.2)");
}

TEST_CASE("gpu_missing: each feature is named") {
    render::GpuSupport g = full_gpu("C");
    g.synchronization2 = false;
    g.descriptor_binding_variable_count = false;
    g.separate_depth_stencil_layouts = false;
    g.presents = false;
    const auto missing = render::gpu_missing(g);
    REQUIRE(missing.size() == 4);
    CHECK(missing[0] == "synchronization2");
    CHECK(missing[1] == "variable descriptor counts");
    CHECK(missing[2] == "separate depth and stencil layouts");
    CHECK(missing[3] == "drawing to this window");
}

TEST_CASE("no_usable_gpu_text lists every GPU and what it lacks") {
    render::GpuSupport old;
    old.name = "Old Card";
    old.api_major = 1;
    old.api_minor = 1;
    render::GpuSupport soft = full_gpu("Software");
    soft.presents = false;
    const std::string text = render::no_usable_gpu_text({old, soft});
    CHECK(text == "no usable GPU:\n- Old Card: lacks Vulkan 1.3 (its driver offers 1.1)\n"
                  "- Software: lacks drawing to this window");
    CHECK(render::no_usable_gpu_text({}) == "no usable GPU: Vulkan found no GPU at all");
}

TEST_CASE("classify_startup_error knows the texts the client throws") {
    using P = app::StartupProblem;
    // As render/vk_context.cpp, render/gpu_requirements.cpp and platform/window.cpp write them.
    CHECK(app::classify_startup_error("volkInitialize (is a Vulkan driver installed?) failed: VkResult -3") ==
          P::NoVulkan);
    CHECK(app::classify_startup_error("vkCreateInstance failed: VkResult -9") == P::NoVulkan);
    CHECK(app::classify_startup_error(
              "SDL_CreateWindow failed: Installed Vulkan doesn't implement the VK_KHR_surface extension") ==
          P::NoVulkan);
    CHECK(app::classify_startup_error("Vulkan 1.3 is required; the loader reports 1.2") == P::OldVulkan);
    CHECK(app::classify_startup_error(render::no_usable_gpu_text({})) == P::NoUsableGpu);
    CHECK(app::classify_startup_error("SDL_Init failed: no available video device") == P::Window);
    CHECK(app::classify_startup_error("SDL_CreateWindow failed: x") == P::Window);
    CHECK(app::classify_startup_error("SDL_Vulkan_CreateSurface: x") == P::Window);
    CHECK(app::classify_startup_error("cannot open shader /x/shaders/zone.vert.spv") == P::MissingFiles);
    CHECK(app::classify_startup_error("bad SPIR-V size in /x") == P::MissingFiles);
    CHECK(app::classify_startup_error("vkCreateDevice failed: VkResult -7") == P::GraphicsError);
    CHECK(app::classify_startup_error("no usable depth format") == P::GraphicsError);
    CHECK(app::classify_startup_error("something else") == P::Other);
}

TEST_CASE("startup_error_text: headline, advice, the error and the log") {
    const std::string gpu_error = render::no_usable_gpu_text({full_gpu("X")});
    const std::string text = app::startup_error_text(gpu_error, std::filesystem::path("logs") / "hailstone.log");
    CHECK(text.rfind("No graphics card on this computer can run Hailstone.", 0) == 0);
    CHECK(contains(text, "System requirements"));
    CHECK(contains(text, "What went wrong:\nno usable GPU:"));
    CHECK(contains(text, "The log has more: logs"));
    CHECK(contains(text, "hailstone.log"));

    const std::string other = app::startup_error_text("boom", {});
    CHECK(other.rfind("Hailstone stopped because of an error.", 0) == 0);
    CHECK(contains(other, "What went wrong:\nboom"));
    CHECK_FALSE(contains(other, "The log has more"));
}

TEST_CASE("Vulkan instance extensions: the portability opt-in only where offered, a missing one named") {
    using render::choose_instance_extensions;
    const std::string kSurface = "VK_KHR_surface", kMetal = "VK_EXT_metal_surface",
                      kPortability = "VK_KHR_portability_enumeration";
    // SDL on macOS asks for all three. MoltenVK loaded directly offers no portability enumeration: the instance is
    // made without it (and without its flag), not refused.
    auto direct = choose_instance_extensions({kSurface, kMetal, kPortability}, {kSurface, kMetal, "VK_EXT_debug_utils"});
    CHECK(direct.enable == std::vector<std::string>{kSurface, kMetal});
    CHECK(direct.missing.empty());
    CHECK_FALSE(direct.portability);
    // Through the Vulkan loader it is offered: enabled once, with its flag, whether or not SDL asked.
    auto loader = choose_instance_extensions({kSurface, kMetal, kPortability}, {kSurface, kMetal, kPortability});
    CHECK(loader.enable == std::vector<std::string>{kSurface, kMetal, kPortability});
    CHECK(loader.portability);
    auto unasked = choose_instance_extensions({kSurface}, {kSurface, kPortability});
    CHECK(unasked.enable == std::vector<std::string>{kSurface, kPortability});
    CHECK(unasked.portability);
    // Another wanted one the library lacks is named, once.
    auto lacking = choose_instance_extensions({kSurface, "VK_KHR_xlib_surface", "VK_KHR_xlib_surface"}, {kSurface});
    CHECK(lacking.enable == std::vector<std::string>{kSurface});
    CHECK(lacking.missing == std::vector<std::string>{"VK_KHR_xlib_surface"});
    // The error it makes is the no-Vulkan dialog's.
    CHECK(app::classify_startup_error("vkCreateInstance: the Vulkan library lacks VK_KHR_xlib_surface") ==
          app::StartupProblem::NoVulkan);
}

