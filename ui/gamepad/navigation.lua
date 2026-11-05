-- The zone guide's windows (gamepad pack), standing in for the stock windows and answered through them
-- (lib/model.lua, app/lua_eq_zoneguide.cpp):
--   Find (FindLocationWnd)       what can be found in this zone (people, points of interest, the ways out): A on one
--                                starts the path to it (the window then closes, as the game's does), and "Only what I
--                                can use" (the filter check box). EQEmu sends no zone guide, so the window says there
--                                is nothing easily found here
--   Zone Path (ZonePathWnd)      the steps of the zone guide's active path (the game refuses to show it without one)
local T = dofile("lib/theme.lua")
local pad = dofile("lib/pad.lua")
local W = dofile("lib/window.lua")
local M = dofile("lib/model.lua")
local C = T.C

-- ---------------------------------------------------------------- find (FindLocationWnd)
local FW = "FindLocationWnd"
local places = {}

ui.window("Find", function()
  if not W.shown(FW) then return end
  T.frame()
  if W.begin("Find", { w = 1200, h = 860, title = "Find", subtitle = "Places, people and ways out of this zone",
        hints = { { "a", "Go there" }, { "b", "Close" } } }) then
    local x0, y0, x1, y1 = W.area()
    local wx, wy = ui.GetWindowPos()
    local bh = T.px(76)
    ui.SetCursorPos(x0 - wx, y0 - wy)
    M.toggle(FW, "FLW_FilterLocationsCheckbox", "Only show destinations I can use", x1 - x0, bh)
    local top = y0 + bh + T.px(18)
    if M.shown(FW, "FLW_FindLocationList") then
      M.list(FW, "FLW_FindLocationList", places, x0, top, x1, y1, { empty = "Nothing found yet." })
    else
      T.font(24)
      local none = M.shown(FW, "FLW_FoundNoneLabel") and eq.ChildShown(FW, "FLW_FoundNoneLabel") == true
      T.draw(x0, top + T.px(10), none and "There is nothing easily found in this zone." or "Looking...", C.dim)
      T.unfont()
    end
  end
  if W.finish() then eq.ShowWindow(FW, false) end
end)

-- ---------------------------------------------------------------- the zone path (ZonePathWnd)
local ZW = "ZonePathWnd"
local steps = {}

ui.window("Zone Path", function()
  if not W.shown(ZW) then return end
  T.frame()
  if W.begin("Zone Path", { w = 1000, h = 800, title = "Zone Path", subtitle = "The zones on the way",
        hints = { { "b", "Close" } } }) then
    local x0, y0, x1, y1 = W.area()
    M.list(ZW, "ZPW_ZoneList", steps, x0, y0, x1, y1, { empty = "No path is set." })
  end
  if W.finish() then eq.ShowWindow(ZW, false) end
end)
