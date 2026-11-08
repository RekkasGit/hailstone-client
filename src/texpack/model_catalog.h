#pragma once

// The image models the texture studio can use (docs/texstudio.md "Models"): ncnn models, each a <id>.param and
// <id>.bin with an optional <id>.ini beside them saying what it is:
//
//   title=PBRify SPAN 4x
//   kind=color          color (an upscaler), normal or rough (1x maps made from the colour)
//   scale=4             the colour model's scale (found by running it when missing)
//   license=CC0-1.0
//   source=https://github.com/Kim2091/PBRify_Remix
//   note=Made for 2000s game textures; removes DXT1 blocks and dithering.
//
// A model without an .ini is taken as a colour model named after its file. The shipped models are in the install's
// texstudio/models/; the player's own go in the data folder's texstudio/models/, and one of the same id wins.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hailstone::texpack {

enum class ModelKind : std::uint8_t { Color, Normal, Rough };
std::string_view model_kind_name(ModelKind kind);

struct ModelEntry {
    std::string id; // the file name without .param
    std::string title, license, source, note;
    ModelKind kind = ModelKind::Color;
    std::uint32_t scale = 0; // 0: not said
    std::filesystem::path param, bin;
    bool user = false; // from the player's folder
};

// The models of the folders, later folders' ids over earlier ones', sorted by kind then title.
std::vector<ModelEntry> list_models(const std::vector<std::filesystem::path>& folders);
const ModelEntry* find_model(const std::vector<ModelEntry>& models, std::string_view id);

// The ids Hailstone ships and its presets use.
inline constexpr const char* kPbrifyColor = "4x-PBRify_UpscalerSPANV4";
inline constexpr const char* kPbrifyNormal = "1x-PBRify_NormalV3";
inline constexpr const char* kPbrifyRough = "1x-PBRify_RoughnessV2";

} // namespace hailstone::texpack
