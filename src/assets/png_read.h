#pragma once

// A PNG reader for replacement textures (assets/texture_overrides.h); EQ's own archives hold none. Every colour
// type (grey, RGB, palette, grey + alpha, RGBA), every bit depth (1 to 16; 16-bit keeps its high byte), tRNS
// transparency and Adam7 interlacing, decoded to RGBA8 with the top row first. The chunks' CRCs are not checked.

#include <cstddef>
#include <optional>
#include <span>
#include <string>

#include "assets/texture.h"

namespace nmsc::assets {

bool is_png(std::span<const std::byte> data);

// Returns nullopt and sets *error when the data is not a PNG this reader takes.
std::optional<Image> decode_png(std::span<const std::byte> data, std::string* error = nullptr);

} // namespace nmsc::assets
