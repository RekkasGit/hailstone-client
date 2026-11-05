-- A block of STML text (a task's description, a mail message) in a panel the pad can focus and scroll: D-pad up /
-- down scroll it while it has the focus, as does the mouse wheel over it. Line breaks are <BR> (and '\n'); colours
-- and links as lib/stml.lua draws them.
--
--   local P = dofile("lib/textpanel.lua")
--   P.draw("##desc", state, stml, x0, y0, x1, y1)   -- state = { scroll = 0 } kept by the caller; returns focused
local T = dofile("lib/theme.lua")
local pad = dofile("lib/pad.lua")
local S = dofile("lib/stml.lua")
local C = T.C
local P = {}

-- The text's lines (split at <BR> and line breaks), each parsed into coloured pieces; the last few texts' kept (two
-- panels in one window take turns).
local cache, order = {}, {}
function P.lines(stml)
  stml = stml or ""
  if cache[stml] then return cache[stml] end
  local lines = {}
  local t = stml:gsub("<[Bb][Rr]%s*/?>", "\n"):gsub("\r", "")
  for l in (t .. "\n"):gmatch("([^\n]*)\n") do lines[#lines + 1] = S.parse(l) end
  cache[stml] = lines
  order[#order + 1] = stml
  if #order > 8 then cache[table.remove(order, 1)] = nil end
  return lines
end

-- Every row of the text laid out in width (S.blockRows keeps them while the text, width and font stay).
local function layout(stml, width)
  local rows = {}
  for _, l in ipairs(P.lines(stml)) do
    for _, r in ipairs(S.layout(l, width)) do rows[#rows + 1] = r end
  end
  return rows
end

-- The plain text of an STML string (no tags), one line.
function P.plain(stml)
  local out = {}
  for _, l in ipairs(P.lines(stml)) do out[#out + 1] = S.plain(l) end
  return table.concat(out, " ")
end

-- Draws `stml` in x0, y0 .. x1, y1 as a focusable panel (an ImGui item at those coordinates); returns focused.
-- opts: size (font, design px; default 21), empty (the text shown when there is none).
function P.draw(id, state, stml, x0, y0, x1, y1, opts)
  opts = opts or {}
  state.scroll = state.scroll or 0
  local wx, wy = ui.GetWindowPos()
  ui.SetCursorPos(x0 - wx, y0 - wy)
  ui.InvisibleButton(id, math.max(1, x1 - x0), math.max(1, y1 - y0), ui.ButtonFlags.EnableNav)
  local focused = pad.focused()
  T.rect(x0, y0, x1, y1, C.sunken, 1, T.px(14))
  if focused then T.focusRing(x0, y0, x1, y1, T.px(14)) end
  T.font(opts.size or 21)
  local _, lh = ui.CalcTextSize("Ag")
  local p = T.px(16)
  local rows = (stml and stml ~= "") and S.blockRows(stml, x1 - x0 - 2 * p, layout) or {}
  local fit = math.max(1, math.floor((y1 - y0 - 2 * p) / lh))
  local maxScroll = math.max(0, #rows - fit)
  if focused then
    if pad.menuPressed("down") then state.scroll = state.scroll + 3 end
    if pad.menuPressed("up") then state.scroll = state.scroll - 3 end
  end
  if ui.IsItemHovered() then
    local wheel = ui.GetIO().wheel or 0
    if wheel ~= 0 then state.scroll = state.scroll - math.floor(wheel * 3 + 0.5) end
  end
  state.scroll = math.max(0, math.min(maxScroll, state.scroll))
  if #rows == 0 then
    T.draw(x0 + p, y0 + p, opts.empty or "", C.faint)
  else
    ui.PushClipRect(x0 + T.px(4), y0 + T.px(4), x1 - T.px(4), y1 - T.px(4), true)
    local y = y0 + p
    for i = state.scroll + 1, math.min(#rows, state.scroll + fit) do
      S.drawRow(rows[i], x0 + p, y)
      y = y + lh
    end
    ui.PopClipRect()
  end
  -- the scroll bar on the right
  if maxScroll > 0 then
    local sx, h = x1 - T.px(10), y1 - y0 - 2 * p
    T.rect(sx, y0 + p, sx + T.px(5), y1 - p, C.raised, 1, T.px(3))
    local th = math.max(T.px(30), h * fit / #rows)
    local ty = y0 + p + (h - th) * (state.scroll / maxScroll)
    T.rect(sx, ty, sx + T.px(5), ty + th, C.dim, 0.8, T.px(3))
  end
  T.unfont()
  return focused
end

return P
