-- Quantity: replaces the game's QuantityWnd (how many to pick up, buy, sell, split...). A number box and a slider
-- that follow each other, quick picks (1, half, all), Accept and Cancel. It opens where the game opens it (by the
-- slot or button that asked), each time.
-- The slider, the box and Accept are the original's (eq.SetSlider / eq.SetWindowText / eq.ClickChild on
-- QTYW_Slider, QTYW_SliderInput, QTYW_Accept_Button): Enter in the box accepts as Accept does, closing the window
-- cancels (CQuantityWnd).
local hud = dofile("lib/hud.lua")
local W = dofile("lib/widgets.lua")
local WINDOW = "QuantityWnd"

local openX, openY = nil, nil -- where the game put the dialog when it last opened
local base = 0                -- the slider's minimum (the box shows slider + minimum)
local wasShown = false
local focusBox, boxActive = false, false

local WINDOW_OPTS = {
  rollup = false, -- (a question to answer: kept whole)
  size = function() return 250, 118 end,
  resizable = { 200, 110 },
  place = function(W_, H, w, h, s, x, y) return openX or x, openY or y end,
}
local function drawWindow(x, y, w, h, s)
  local _, _, value, count = eq.ChildState(WINDOW, "QTYW_Slider")
  value, count = value or 0, count or 1
  local max = base + count - 1
  local _, lh = ui.CalcTextSize("Ag")
  local btnH = lh + 10 * s
  -- the number, focused (and selected) when the dialog opens: type and press Enter
  local bw = 46 * s
  if focusBox then
    ui.SetKeyboardFocusHere()
    focusBox = false
  end
  W.Edit(WINDOW, "QTYW_SliderInput", { hint = "Amount", width = -(3 * (bw + 6 * s)) })
  local active = ui.IsItemActive()
  if ui.IsItemHovered() then ui.SetTooltip(string.format("%d to %d; Enter accepts", base, max)) end
  if boxActive and ui.IsKeyPressed(ui.Key.Enter) then
    boxActive = false
    eq.WindowButton(WINDOW, "QTYW_SliderInput")
    return
  end
  boxActive = active
  for _, q in ipairs({ { "1", 1 - base, "One" }, { "Half", (count - 1) // 2, "Half of them" }, { "All", count - 1, "All of them" } }) do
    ui.SameLine(0, 6 * s)
    if ui.Button(q[1] .. "##q", bw, 0) then eq.SetSlider(WINDOW, "QTYW_Slider", math.max(0, math.min(count - 1, q[2]))) end
    if ui.IsItemHovered() then ui.SetTooltip(q[3]) end
  end
  -- the slider
  W.Slider(WINDOW, "QTYW_Slider", { format = function(v) return tostring(v + base) .. " of " .. max end })
  ui.Dummy(0, 2 * s)
  -- Accept, and Cancel (closing the window cancels, as the original's close box does)
  local half = (ui.GetContentRegionAvail() - 8) / 2
  W.Button(WINDOW, "QTYW_Accept_Button", "Accept", { primary = true, width = half, height = btnH })
  ui.SameLine()
  if ui.Button("Cancel##qty", half, btnH) then eq.ShowWindow(WINDOW, false) end
end

ui.window("Quantity", function()
  if not eq.InGame() then return end
  -- The game moves the dialog to its caller each time it opens (eq.ReplaceWindow's `place`): forget the place it
  -- was dragged to, so it opens there.
  local shown, sx, sy, _, _, _, place = eq.ReplaceWindow(WINDOW)
  if place then
    openX, openY = sx, sy
    ui.SetSetting("hud.pos." .. WINDOW, "")
  end
  if shown and not wasShown then
    -- the box starts at the slider's value plus the minimum
    local _, _, value = eq.ChildState(WINDOW, "QTYW_Slider")
    base = (tonumber(W.Text(WINDOW, "QTYW_SliderInput")) or 0) - (value or 0)
    focusBox, boxActive = true, false
  end
  wasShown = shown
  hud.Dialog(WINDOW, "Quantity", WINDOW_OPTS, drawWindow)
end)
