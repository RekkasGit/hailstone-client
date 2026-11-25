#pragma once

// ui/lua/lua_imgui.cpp
struct ImDrawList;
struct ImVec2;

namespace hailstone::ui::lua {

// Hide the ui.Draw* marks a UI pack or add-on puts over vanilla maps. Only primitives on a map's own draw list and in
// the same frame are affected; every map drawn in that frame keeps its own area. Outlines on the area's boundary and
// pack controls submitted after the map's hit box stay visible, so minimap rings, north markers, zoom controls, map
// borders and clocks keep drawing. C++ map drawing does not go through this guard.
void suppress_map_draws(ImDrawList* list, ImVec2 min, ImVec2 max);
void suppress_map_draws(ImDrawList* list, ImVec2 center, float radius);

// The ui.Draw* functions draw on the current window's own list, unless the code around them asked for
// ui.Foreground(true) in this frame, which draws above every window. A retained callback that must stay inside the
// window it is drawn into (a map overlay inside the map's clip, whichever add-on's map code called it in) holds this
// for as long as it draws: its own ui.Foreground(true) still takes effect, an inherited one does not.
struct WindowDrawScope {
    WindowDrawScope();
    ~WindowDrawScope();
    WindowDrawScope(const WindowDrawScope&) = delete;
    WindowDrawScope& operator=(const WindowDrawScope&) = delete;

private:
    int before_;
};

} // namespace hailstone::ui::lua
