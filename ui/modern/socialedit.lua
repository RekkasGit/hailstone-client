-- Edit social: replaces the game's SocialEditWnd (a right click on a social's hot button or Actions window button).
-- The social's name (shown in its colour), the twenty name colours as swatches (the one chosen ringed), its five
-- command lines, Clear (empties the name and lines) and Accept (saves the social and closes). The boxes, colours and
-- buttons are the window's (app/lua_eq_socialedit.cpp: eq.SetWindowText, eq.ClickChild), so what is saved is the
-- game's.
local hud = dofile("lib/hud.lua")
local W = dofile("lib/widgets.lua")
local K = dofile("lib/dialogs_kit.lua")
local P = dofile("lib/progress.lua")
local T = hud.T
local C = T.color
local WINDOW = "SocialEditWnd"
local LINES = { "First", "Second", "Third", "Fourth", "Fifth" }

local function near(a, b) return math.abs(a - b) < 0.01 end

local OPTS = {
  size = function() return 420, 330 end,
  resizable = { 320, 300 },
}
local function body(x, y, w, h, s)
  local colors = eq.SocialColors() or {}
  local nr, ng, nb = eq.ChildColor(WINDOW, "SEW_NameInput")
  -- the name, in its colour
  K.Caption("Name")
  ui.PushStyleColor(ui.Col.Text, nr or C.text[1], ng or C.text[2], nb or C.text[3], 1)
  W.Edit(WINDOW, "SEW_NameInput", { hint = "The button's label (15 letters)", max = 16 })
  ui.PopStyleColor(1)
  -- the colours: SEW_TC0Button .. SEW_TC19Button
  K.Caption("Colour")
  local aw = ui.GetContentRegionAvail()
  local gap = 4 * s
  local perRow = 10
  local sw = math.min(28 * s, (aw - gap * (perRow - 1)) / perRow)
  for i, c in ipairs(colors) do
    if (i - 1) % perRow ~= 0 then ui.SameLine(0, gap) end
    local chosen = nr and near(c[1], nr) and near(c[2], ng) and near(c[3], nb)
    if K.Swatch("##sewc" .. i, c[1], c[2], c[3], sw * 0.8, s, { width = sw, selected = chosen,
          tooltip = "Colour " .. (i - 1) }) then
      eq.ClickChild(WINDOW, "SEW_TC" .. (i - 1) .. "Button", "left")
    end
  end
  -- the lines
  K.Caption("Commands (one a line, run in turn)")
  for n = 0, 4 do
    ui.TextColored(C.faint[1], C.faint[2], C.faint[3], 1, tostring(n + 1))
    ui.SameLine(28 * s)
    W.Edit(WINDOW, "SEW_Line" .. n .. "Input", { hint = LINES[n + 1] .. " command, e.g. /say Hail", max = 256 })
  end
  local bh = K.ButtonH(s)
  local _, cy = ui.GetCursorScreenPos()
  ui.SetCursorScreenPos(x, math.max(cy + 6 * s, y + h - bh))
  W.ButtonRow(WINDOW, {
    { "SEW_Clear_Button", "Clear", { tooltip = "Empty the name and every command" } },
    { "SEW_Accept_Button", "Accept", { primary = true, tooltip = "Save the social" } },
  }, bh)
end

local function testOpen() eq.OpenSocialEditor(tonumber(ui.GetSetting("test.social") or "") or 0) end
ui.window("Edit Social", function()
  if not eq.InGame() then return end
  P.TestOpen(WINDOW, testOpen)
  hud.Dialog(WINDOW, "Edit Social", OPTS, body)
end)
