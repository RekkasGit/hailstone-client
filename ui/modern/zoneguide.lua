-- The zone guide's windows: FindLocationWnd (Find: the people, places and ways out of this zone; a row clicked
-- starts the path to it) and ZonePathWnd (the zones on the way to the zone guide's destination). Find: the places
-- as a table (category, description), "Only show destinations I can use", and while the zone has none the
-- original's "nothing easily found" line (shown as the window says; before the answer, "Looking..."). The lists and
-- check box are the original's (app/lua_eq_zoneguide.cpp). EQEmu sends no zone guide, so Find finds nothing there
-- and Zone Path never opens (the game refuses without a path).
local hud = dofile("lib/hud.lua")
local W = dofile("lib/widgets.lua")
local K = dofile("lib/quests_kit.lua")
local P = dofile("lib/progress.lua")
local T = hud.T
local C = T.color
local FIND = "FindLocationWnd"
local PATH = "ZonePathWnd"

-- testing: test.draw.<screen>=1 draws the window though the game has not opened it (Zone Path without a path)
local drawKeys = {}
local function forced(win)
  local key = drawKeys[win]
  if not key then key = "test.draw." .. win; drawKeys[win] = key end
  return T.Setting(key) == "1" or nil
end

local function testOpen() eq.ShowWindow(FIND, true) end
local OPTS = {
  size = function() return 380, 340 end,
  resizable = { 260, 200 },
}
local function body(x, y, w, h, s)
  local _, lh = ui.CalcTextSize("Ag")
  local footer = lh + 14 * s
  if W.Shown(FIND, "FLW_FindLocationList") then
    W.List(FIND, "FLW_FindLocationList", 0, -footer, {
      sortable = true,
      columns = { [1] = { "Category", 100 }, [2] = { "Destination", 0 } },
    })
  else
    ui.BeginChild("##findempty", 0, -footer, false)
    if W.Shown(FIND, "FLW_FoundNoneLabel") then
      local none = W.Text(FIND, "FLW_FoundNoneLabel")
      K.Empty(none ~= "" and none or "There is nothing easily found in this zone.")
    else
      K.Empty("Looking...")
    end
    ui.EndChild()
  end
  W.Check(FIND, "FLW_FilterLocationsCheckbox", "Only show destinations I can use",
    { tooltip = "Leave out the places you cannot travel to" })
end

ui.window("Find", function()
  P.TestOpen(FIND, testOpen) -- testing: test.open.FindLocationWnd=1
  if not eq.InGame() then return end
  OPTS.shown = forced(FIND)
  hud.Dialog(FIND, "Find", OPTS, body)
end)

local PATH_OPTS = {
  size = function() return 280, 300 end,
  resizable = { 200, 160 },
}
local function pathBody(x, y, w, h, s)
  K.Muted("The zones on the way to your destination:", C.muted)
  ui.Dummy(0, 2 * s)
  local rows = eq.ListInfo(PATH, "ZPW_ZoneList") or 0
  if rows == 0 then
    ui.BeginChild("##pathempty", 0, 0, false)
    K.Empty("No path.", "Find a place in another zone to get one.")
    ui.EndChild()
  else
    W.List(PATH, "ZPW_ZoneList", 0, 0, {})
  end
end

ui.window("Zone Path", function()
  if not eq.InGame() then return end
  PATH_OPTS.shown = forced(PATH)
  hud.Dialog(PATH, "Zone Path", PATH_OPTS, pathBody)
end)
