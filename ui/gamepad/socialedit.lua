-- Edit Social (gamepad pack): stands in for SocialEditWnd (a hot button's Assign Social with a new social, a right
-- click on a social). The social's name and its five command lines as fields the on-screen keyboard types into
-- (the window's edit boxes: 15 and 255 characters), its colour as a row of the window's twenty colour buttons, and
-- the window's Clear and Accept (Accept saves it, as the stock button does). A Lua social opens the Lua social
-- editor instead (luasocial.lua), as in every pack.
local T = dofile("lib/theme.lua")
local pad = dofile("lib/pad.lua")
local W = dofile("lib/window.lua")
local CH = dofile("lib/child.lua")
local C = T.C
local WIN = "SocialEditWnd"

local st = CH.new()
local wasShown = false

-- The colour button whose colour the name box has (the social's colour; -1 when none matches).
local function currentColour(colours)
  local r, g, b = eq.ChildColor(WIN, "SEW_NameInput")
  if not r then return -1 end
  for i, c in ipairs(colours) do
    if math.abs(c[1] - r) < 0.02 and math.abs(c[2] - g) < 0.02 and math.abs(c[3] - b) < 0.02 then return i - 1 end
  end
  return -1
end

ui.window("Edit Social", function()
  CH.testOpen(WIN)
  if not W.shown(WIN) then
    st.kb.open, st.pick, wasShown = false, nil, false
    return
  end
  T.frame()
  local appearing = not wasShown
  wasShown = true
  if W.begin("Edit Social", { w = 1400, h = 960, title = "Edit Social", subtitle = "A hot button that types its lines",
        hints = CH.hints(st, { { "a", "Edit" }, { "b", "Close" } }) }) then
    local x0, y0, x1, y1, cw = W.area()
    local wx, wy = ui.GetWindowPos()
    if not CH.modal(st, x0, y0, x1, y1) then
      local fh = T.px(84)
      ui.SetCursorPos(x0 - wx, y0 - wy)
      if appearing then ui.SetKeyboardFocusHere() end
      CH.field(st, WIN, "SEW_NameInput", "Name", cw, fh, { max = 15, capital = true, prompt = "The social's name", keep = true })
      -- the colours: ten a row, each the colour it gives the name
      local colours = eq.SocialColors and eq.SocialColors() or {}
      local cur = currentColour(colours)
      local cy = y0 + fh + T.px(16)
      local th = CH.caption(x0, cy, "Colour")
      cy = cy + th + T.px(6)
      local n, gap = 10, T.px(10)
      local sw = (cw - (n - 1) * gap) / n
      local sh = T.px(56)
      local name = CH.text(WIN, "SEW_NameInput")
      for i = 0, 19 do
        local id = "SEW_TC" .. i .. "Button"
        ui.SetCursorPos(x0 + (i % n) * (sw + gap) - wx, cy + (i // n) * (sh + gap) - wy)
        local clicked, focused, bx0, by0, bx1, by1 = T.button("##" .. id, "", sw, sh, {}, pad)
        local c = colours[i + 1] or { 1, 1, 1 }
        T.font(20)
        T.textIn(bx0 + T.px(6), by0, bx1 - T.px(6), by1, T.fit(name ~= "" and name or "Social", sw - T.px(12)), { c[1], c[2], c[3], 1 }, 0.5)
        T.unfont()
        if i == cur then T.outline(bx0 + T.px(2), by0 + T.px(2), bx1 - T.px(2), by1 - T.px(2), C.accent, 1, T.px(10), T.px(3)) end
        if clicked then eq.ClickChild(WIN, id, "left") end
      end
      -- the five lines
      local ly = cy + 2 * (sh + gap) + T.px(10)
      th = CH.caption(x0, ly, "Lines (each is typed as a chat line, in turn)")
      ly = ly + th + T.px(6)
      local lh = T.px(70)
      for i = 0, 4 do
        ui.SetCursorPos(x0 - wx, ly + i * (lh + T.px(8)) - wy)
        CH.field(st, WIN, "SEW_Line" .. i .. "Input", "Line " .. (i + 1), cw, lh, { max = 255, keep = true,
          prompt = "Line " .. (i + 1) .. " (a chat line or a /command)" })
      end
      local bh = T.px(72)
      ui.SetCursorPos(x0 - wx, y1 - bh - wy)
      local bw = (cw - T.px(12)) / 2
      CH.button(WIN, "SEW_Clear_Button", "Clear", bw, bh, { sub = "Empty the name and the lines", align = 0, keep = true })
      ui.SameLine(0, T.px(12))
      CH.button(WIN, "SEW_Accept_Button", "Accept", bw, bh, { primary = true, keep = true })
    end
  end
  if CH.back(st, W.finish()) then eq.ShowWindow(WIN, false) end
end)
