#include "app/window_icon.h"

#include <cstdint>
#include <span>

#include "assets/png_read.h"
#include "core/game_dir.h"
#include "core/log.h"
#include "platform/paths.h"
#include "platform/window.h"

namespace hailstone::app {

std::optional<assets::Image> read_window_icon(const std::filesystem::path& file, std::string* error) {
    auto fail = [&](std::string why) -> std::optional<assets::Image> {
        if (error) *error = std::move(why);
        return std::nullopt;
    };
    auto data = read_file(file);
    if (!data) return fail("cannot read " + file.string());
    std::string why;
    auto img = assets::decode_png(*data, &why);
    if (!img) return fail(why);
    if (img->format != assets::PixelFormat::RGBA8 || img->mips.empty() || img->width == 0 || img->height == 0)
        return fail("not an RGBA image");
    if (img->width > kMaxWindowIconSide || img->height > kMaxWindowIconSide) return fail("larger than 1024 pixels");
    if (img->mips[0].size() != std::size_t{img->width} * img->height * 4) return fail("pixel data the wrong size");
    return img;
}

void apply_window_icon(platform::Window& window) {
    const std::filesystem::path file = platform::icons_dir() / "hailstone.png";
    std::string why;
    auto img = read_window_icon(file, &why);
    if (!img) {
        log::warn("window icon: {}", why);
        return;
    }
    const auto& px = img->mips[0];
    window.set_icon(static_cast<int>(img->width), static_cast<int>(img->height),
                    std::span(reinterpret_cast<const std::uint8_t*>(px.data()), px.size()));
}

} // namespace hailstone::app
