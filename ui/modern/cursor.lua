-- Cursor: what the game carries on the mouse (an item, a spell from the book, a hot button being dragged;
-- eq.CursorAttachment) drawn above every window in this UI's look: the icon in a rounded well, a hot button's label
-- on its face (wrapped, as on the action bars), and a stack's size in a small pill at the bottom right. Places and
-- sizes are the game's attachment's (the icon at the mouse).
local T = dofile("lib/theme.lua")
local C = T.color

local function wrap(text, width, font)
  local lines, line = {}, ""
  for wd in text:gmatch("%S+") do
    local try = line == "" and wd or (line .. " " .. wd)
    if T.TextSize(try, font) > width and line ~= "" then
      lines[#lines + 1] = line
      line = wd
    else
      line = try
    end
  end
  if line ~= "" then lines[#lines + 1] = line end
  return lines
end

ui.window("Cursor Item", function()
  if not eq.InGame() then return end
  local a = eq.CursorAttachment()
  if not a then return end
  local s = ui.GetIO().uiScale or 1
  ui.Foreground(true)
  local face = a.bgRect or a.iconRect or a.rect
  if face then
    local x0, y0, sz = face[1], face[2], face[3]
    local tex, u0, v0, u1, v1
    if a.icon then tex, u0, v0, u1, v1 = a.icon[1], a.icon[2], a.icon[3], a.icon[4], a.icon[5] end
    ui.DrawRectFilled(x0 + 2, y0 + 3, x0 + sz + 2, y0 + sz + 4, 0, 0, 0, 0.35, 7) -- a soft shadow: it is lifted
    T.Icon(x0, y0, sz, s, tex, u0, v0, u1, v1, a.button and C.accent or { C.ink[1], C.ink[2], C.ink[3], 0.25 }, { radius = 6 })
    if a.button and not tex and a.text and a.text ~= "" then
      ui.DrawRectFilled(x0 + 1, y0 + 1, x0 + sz - 1, y0 + sz - 1, C.accent[1], C.accent[2], C.accent[3], 0.12, 6)
      local lines = wrap(a.text, sz - 4, T.font.tiny)
      local _, lh = T.TextSize("Ag", T.font.tiny)
      local n = math.min(#lines, 3)
      local ty = y0 + (sz - n * lh) / 2
      for k = 1, n do T.Text(x0 + sz / 2, ty + (k - 1) * lh, lines[k], C.text, T.font.tiny, "center") end
    end
    if a.quantity then
      local text = T.Thousands(a.quantity)
      local tw, th = T.TextSize(text, T.font.tiny)
      local px1, py1 = x0 + sz + 2, y0 + sz + 2
      local px0, py0 = px1 - tw - 8, py1 - th - 2
      ui.DrawRectFilled(px0, py0, px1, py1, C.popup[1], C.popup[2], C.popup[3], 0.92, (th + 2) / 2)
      ui.DrawRect(px0, py0, px1, py1, C.ink[1], C.ink[2], C.ink[3], 0.18, (th + 2) / 2, 1)
      T.Text(px0 + 4, py0 + 1, text, C.text, T.font.tiny)
    end
  end
  ui.Foreground(false)
end)
