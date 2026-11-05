-- The modern look for ImGui's own pieces: window frames and title bars, buttons, fields, tabs, scroll bars, popups and
-- tooltips. style.Push() before a window's ui.Begin, style.Pop() after its ui.End. Used around the HUD frames and
-- the dialogs, so they read as one UI.
-- Colours the running client does not name in ui.Col (an older build) are skipped.
local T = dofile("lib/theme.lua")
local C = T.color

local style = {}

-- ImGui's colours from the palette's (lib/palettes.lua). Worked out again when the palette changes (theme.lua's
-- modern_palette_gen), so a change shows at once.
local function colors()
  local A, I = C.accent, C.ink
  local function a(c, alpha) return { c[1], c[2], c[3], alpha } end
  return {
    Text = C.text,
    TextDisabled = C.muted,
    WindowBg = C.window,
    ChildBg = { 0, 0, 0, 0 },
    PopupBg = C.popup,
    Border = C.border,
    BorderShadow = { 0, 0, 0, 0 },
    FrameBg = C.frame,
    FrameBgHovered = C.frameHover,
    FrameBgActive = C.frameActive,
    TitleBg = C.title,
    TitleBgActive = C.titleActive,
    TitleBgCollapsed = a(C.title, 0.75),
    MenuBarBg = C.popup,
    ScrollbarBg = { 0, 0, 0, 0 },
    ScrollbarGrab = a(I, 0.22),
    ScrollbarGrabHovered = a(I, 0.34),
    ScrollbarGrabActive = a(A, 0.70),
    CheckMark = A,
    SliderGrab = a(A, 0.85),
    SliderGrabActive = A,
    Button = a(I, 0.08),
    ButtonHovered = a(A, 0.26),
    ButtonActive = a(A, 0.40),
    Header = a(A, 0.20),
    HeaderHovered = a(A, 0.28),
    HeaderActive = a(A, 0.38),
    Separator = C.separator,
    SeparatorHovered = a(A, 0.55),
    SeparatorActive = a(A, 0.85),
    ResizeGrip = a(I, 0.10),
    ResizeGripHovered = a(A, 0.50),
    ResizeGripActive = a(A, 0.85),
    Tab = a(I, 0.07),
    TabHovered = a(A, 0.22),
    TabSelected = a(A, 0.42), -- (well apart from a hovered tab: the page shown is the one that stands out)
    TabSelectedOverline = A,
    TabDimmed = a(I, 0.05),
    TabDimmedSelected = a(A, 0.16),
    PlotHistogram = a(A, 0.90),
    PlotHistogramHovered = A,
    TableHeaderBg = a(I, 0.07),
    TableBorderStrong = a(I, 0.16),
    TableBorderLight = a(I, 0.08),
    TableRowBg = { 0, 0, 0, 0 },
    TableRowBgAlt = a(I, 0.03),
    TextSelectedBg = a(A, 0.35),
    NavCursor = a(A, 0.85),
    ModalWindowDimBg = { 0, 0, 0, 0.45 },
  }
end

local VARS = { -- name -> value or { x, y }; scaled by the UI scale where they are sizes
  WindowRounding = 8, ChildRounding = 6, FrameRounding = 5, PopupRounding = 6, ScrollbarRounding = 8,
  GrabRounding = 4, TabRounding = 5, WindowBorderSize = 1, FrameBorderSize = 0, PopupBorderSize = 1,
  ScrollbarSize = 10, WindowTitleAlign = { 0.02, 0.5 }, TabBarBorderSize = 1,
  DisabledAlpha = 0.70, -- a disabled row stays readable (ImGui's 0.6 dims its text below 4.5:1 on light palettes)
}

-- What a push pushes, as flat lists (ui.PushStyleColors / ui.PushStyleVars): { idx, r, g, b, a, ... } and
-- { idx, x, y or false, ... }. Pushed by every frame and dialog every frame, so kept for every script (each dofile of
-- this file makes a new copy of the rest) and worked out again only when the palette changes.
modern_style = modern_style or { gen = nil }
local cache = modern_style

local function build()
  local cl, nc = {}, 0
  for name, c in pairs(colors()) do
    local idx = ui.Col[name]
    if idx then
      cl[nc * 5 + 1], cl[nc * 5 + 2], cl[nc * 5 + 3], cl[nc * 5 + 4], cl[nc * 5 + 5] = idx, c[1], c[2], c[3], c[4]
      nc = nc + 1
    end
  end
  local vl, nv = {}, 0
  for name, v in pairs(VARS) do
    local idx = ui.StyleVar[name]
    if idx then
      if type(v) == "table" then
        vl[nv * 3 + 1], vl[nv * 3 + 2], vl[nv * 3 + 3] = idx, v[1], v[2]
      else
        vl[nv * 3 + 1], vl[nv * 3 + 2], vl[nv * 3 + 3] = idx, v, false
      end
      nv = nv + 1
    end
  end
  cache.colors, cache.nc, cache.vars, cache.nv = cl, nc, vl, nv
  cache.gen = modern_palette_gen
end

-- the counts pushed, a stack so nested pushes pop what they pushed
local pushedColors, pushedVars, depth = {}, {}, 0

function style.Push()
  T.TestPaletteCycle()
  if cache.gen ~= modern_palette_gen or not cache.colors then build() end
  local nc, nv
  if ui.PushStyleColors then
    nc = ui.PushStyleColors(cache.colors)
    nv = ui.PushStyleVars(cache.vars)
  else -- (a client without the batch calls: one at a time)
    local cl, vl = cache.colors, cache.vars
    for i = 1, cache.nc * 5, 5 do ui.PushStyleColor(cl[i], cl[i + 1], cl[i + 2], cl[i + 3], cl[i + 4]) end
    for i = 1, cache.nv * 3, 3 do
      if vl[i + 2] then ui.PushStyleVar(vl[i], vl[i + 1], vl[i + 2]) else ui.PushStyleVar(vl[i], vl[i + 1]) end
    end
    nc, nv = cache.nc, cache.nv
  end
  depth = depth + 1
  pushedColors[depth], pushedVars[depth] = nc, nv
end

function style.Pop()
  if depth == 0 then return end
  local nc, nv = pushedColors[depth], pushedVars[depth]
  depth = depth - 1
  if nv > 0 then ui.PopStyleVar(nv) end
  if nc > 0 then ui.PopStyleColor(nc) end
end

return style
