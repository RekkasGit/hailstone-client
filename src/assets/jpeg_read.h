#pragma once

// A JPEG reader (stb_image) for the pictures behind the pre-game and loading screens (app/screen_pictures.h); the
// game's own files hold none. Baseline and progressive, decoded to RGBA8 with the top row first.

#include <cstddef>
#include <optional>
#include <span>
#include <string>

#include "assets/texture.h"

namespace hailstone::assets {

bool is_jpeg(std::span<const std::byte> data);

// Returns nullopt and sets *error when the data is not a JPEG this reader takes.
std::optional<Image> decode_jpeg(std::span<const std::byte> data, std::string* error = nullptr);

} // namespace hailstone::assets
