#pragma once

// An icon of the stock UI's sheets, or one of the client's fonts, for a plugin's own Dear ImGui drawing (the SDK's
// ui_icon and ui_font): what the app gives plugin::NativeEnv::ui_icon and ui_font, in neither the SDK's types nor
// the UI's, so both sides can include this.

#include <cstdint>

namespace hailstone::plugin {

// The SDK's HAILSTONE_UI_ICON_* (native_plugins.cpp checks they match).
enum class UiIconKind : std::int32_t {
    Spell = 1, // A_SpellIcons (spells01.tga ..., 40 x 40): a spell's SpellIcon
    Gem = 2,   // A_SpellGems (gemicons01.tga ..., 24 x 24): a spell's GemIcon
};

// The SDK's HAILSTONE_UI_FONT_* (native_plugins.cpp checks they match).
enum class UiFontKind : std::int32_t {
    Eq = 1, // EverQuest's font table, 0 .. 10 (ui::chat_font)
};

// The cell: its texture as the client's Dear ImGui takes it (an ImTextureID) and its corners in that texture.
struct UiIconImage {
    std::uintptr_t texture = 0;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
};

} // namespace hailstone::plugin
