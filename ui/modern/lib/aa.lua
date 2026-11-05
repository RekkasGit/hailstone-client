-- Alternate Advancement: replaces the game's AAWindow. A header with the points to spend, the points spent, the AA
-- experience and the share of experience that goes to it (- / +); the game's four tabs (General, Archetype, Class,
-- Special) over its lists, with a search, a category filter, an owned / not owned view and the game's own
-- "trainable only" filter; and the selected ability at the side: its rank, the next rank's cost, the level and the
-- abilities it needs, its reuse time, the game's description, Train (and "all ranks" where the server allows it)
-- and Hotkey. Rows are the original's (eq.ListClick selects in the game's list, so Train and Hotkey act on it;
-- dragging a row puts the ability's hot button on the cursor, as on the stock list); an ability greyed is one the
-- game will not train now. The requirements come from the window's Modern* children (app/lua_eq_progress.cpp).
--
-- A library (as lib/xtarget.lua is), so that a script's window named "Alternate Advancement" can be this one with
-- parts of its own: dofile("lib/aa.lua").window(opts) draws the whole window; opts (all optional):
--   row = {
--     width = n: unscaled pixels kept at the right of each row for the script's part (the name, ranks and cost stop
--       short of it, and so does the row's click and drag);
--     draw = function(r, x0, y0, x1, y1, s): draws that part (its own hit boxes take its clicks);
--     style = function(r): nil, or { ring = {r, g, b, a} (the row's outline), tag = { text, {r, g, b, a} } (a small
--       label after the name) },
--   }
--   details = function(sel, x, y, w, s): draws a part of the script's in the selected ability's panel, under its
--     requirements; returns the height it took (unscaled pixels times s, as drawn).
--   banner = function(x, y, w, s): draws a strip of the script's under the header; returns its height (0 for none).
-- r, a row: name, rank, max, cost (the next rank's, as text; "" at the maximum), category, trainable (the game
-- would train it now), maxed, page (the tab, 0-based). sel, the selected ability: id (its rank's), name, rank, max,
-- cost (text).
local hud = dofile("lib/hud.lua")
local W = dofile("lib/widgets.lua")
local P = dofile("lib/progress.lua")
local T = hud.T
local C = T.color
local WINDOW = "AAWindow"
local PAGES = { { "General", 0 }, { "Archetype", 1 }, { "Class", 2 }, { "Special", 3 } }
local VIEWS = { "All abilities", "Owned", "Not owned", "Not maxed" }
local SIDE_W = 290      -- the details' width beside the list (design pixels)
local STACK_W = 600     -- narrower than this, the details go under the list

local search, view, category = "", 1, nil -- category: the Category column's text, nil any
local needle = ""                           -- the search in lower case, this frame

local function split(text, sep)
  local out = {}
  for part in ((text or "") .. sep):gmatch("(.-)" .. sep) do out[#out + 1] = part end
  return out
end

-- split() of a child's text, kept while the text stays the same (the details read three a frame)
local splits = {}
local function splitKept(slot, text, sep)
  local c = splits[slot]
  if not c or c.text ~= text then
    c = { text = text, parts = split(text, sep) }
    splits[slot] = c
  end
  return c.parts
end

-- Names in lower case for the search, kept (a list's names come back as the same strings each frame).
local lowered, nLowered = {}, 0
local function lowerOf(text)
  local l = lowered[text]
  if not l then
    if nLowered >= 2048 then lowered, nLowered = {}, 0 end
    l = text:lower()
    lowered[text], nLowered = l, nLowered + 1
  end
  return l
end

local NO_TRAIL = { noTrail = true }

-- "cur/max" of the list's second column.
local function ranks(texts)
  local cur, max = (texts[2] or ""):match("(%-?%d+)/(%d+)")
  return tonumber(cur) or 0, tonumber(max) or 0
end

local function seconds(n)
  n = tonumber(n) or 0
  if n <= 0 then return "-" end
  if n >= 3600 then
    local h, m = n // 3600, (n % 3600) // 60
    return m > 0 and string.format("%dh %dm", h, m) or string.format("%dh", h)
  end
  if n >= 60 then
    local m, sec = n // 60, n % 60
    return sec > 0 and string.format("%dm %ds", m, sec) or string.format("%dm", m)
  end
  return n .. "s"
end

-- A row of small rank pips (up to 10; more as a bar) from x, centred on y; returns the width.
local function rankPips(x, y, s, cur, max, w)
  if max <= 0 then return 0 end
  if max > 10 then
    T.Bar("aa.rank", x, y - 2 * s, x + w, y + 2 * s, s, cur / max, C.aa, NO_TRAIL)
    return w
  end
  local gap = 3 * s
  local pw = math.min(10 * s, (w - gap * (max - 1)) / max)
  for i = 1, max do
    local px = x + (i - 1) * (pw + gap)
    if i <= cur then ui.DrawRectFilled(px, y - 2 * s, px + pw, y + 2 * s, C.aa[1], C.aa[2], C.aa[3], 1, 2 * s)
    else ui.DrawRectFilled(px, y - 2 * s, px + pw, y + 2 * s, C.ink[1], C.ink[2], C.ink[3], 0.12, 2 * s) end
  end
  return max * (pw + gap) - gap
end

-- ---------------------------------------------------------------- the header
local POINTS_TILE = { color = C.gold, font = T.font.large, tooltip = "Points you can spend" }
local SPENT_TILE = {}
local LESS_BUTTON = { tooltip = "Less of your experience goes to AA" }
local MORE_BUTTON = { tooltip = "More of your experience goes to AA" }
local assignedText, spentTip = nil, nil

local function header(x, y, w, s)
  local h = 50 * s
  local tileW = 92 * s
  local points = W.Text(WINDOW, "CurrentCount")
  P.Tile("##aapts", x, y, tileW, h, s, "UNSPENT", points ~= "" and points or "0", POINTS_TILE)
  local assigned = W.Text(WINDOW, "AssignedCount")
  if assigned ~= assignedText then
    assignedText, spentTip = assigned, "Points spent on abilities (assigned: " .. assigned .. ")"
  end
  SPENT_TILE.tooltip = spentTip
  P.Tile("##aaspent", x + tileW + 6 * s, y, tileW, h, s, "SPENT", W.Text(WINDOW, "TotalCount"), SPENT_TILE)
  -- the AA experience and the share of experience going to it
  local gx = x + 2 * tileW + 18 * s
  local gw = w - (gx - x)
  local frac, text = eq.Gauge(5)
  T.Text(gx, y + 4 * s, "AA experience", C.muted, T.font.small)
  T.Text(gx + gw, y + 4 * s, frac and string.format("%.1f%%", frac * 100) or (text or ""), C.text, T.font.small,
    "right")
  T.Bar("aa.exp", gx, y + 22 * s, gx + gw, y + 30 * s, s, frac or 0, C.aa, NO_TRAIL)
  local by = y + 33 * s
  T.Text(gx, by + 2 * s, "Experience to AA", C.faint, T.font.small)
  local bw, pctW = 22 * s, 46 * s
  local bx = gx + gw - (2 * bw + pctW)
  ui.SetCursorScreenPos(bx, by)
  LESS_BUTTON.width, LESS_BUTTON.height = bw, 18 * s
  W.Button(WINDOW, "LessExpButton", "-", LESS_BUTTON)
  T.Text(bx + bw + pctW / 2, by + 2 * s, W.Text(WINDOW, "ExpCount"), C.aa, T.font.small, "center")
  ui.SetCursorScreenPos(bx + bw + pctW, by)
  MORE_BUTTON.width, MORE_BUTTON.height = bw, 18 * s
  W.Button(WINDOW, "MoreExpButton", "+", MORE_BUTTON)
  ui.SetCursorScreenPos(x, y + h + 6 * s)
end

-- ---------------------------------------------------------------- the list
-- The categories in the tab's rows (seen: a set, cats: in order), worked out again only when a row's category
-- column changes.
local catRows, seen, cats = {}, {}, {}
local function categories(rows)
  local same = #rows == #catRows
  if same then
    for i, t in ipairs(rows) do
      if (t[4] or "") ~= catRows[i] then
        same = false
        break
      end
    end
  end
  if same then return seen, cats end
  catRows, seen, cats = {}, {}, {}
  for i, t in ipairs(rows) do
    local c = t[4] or ""
    catRows[i] = c
    if c ~= "" and not seen[c] then seen[c] = true; cats[#cats + 1] = c end
  end
  table.sort(cats)
  return seen, cats
end

local CAN_PURCHASE = { tooltip = "The game's filter: abilities you meet the requirements for and can afford" }

local function filters(s, rows)
  -- categories in the tab's rows
  local seen, cats = categories(rows)
  if category and not seen[category] then category = nil end
  local avail = ui.GetContentRegionAvail()
  local comboW = math.min(150 * s, avail * 0.25)
  search = P.Search("##aasearch", search, "Search abilities", avail - 2 * comboW - 16 * s)
  ui.SameLine()
  ui.SetNextItemWidth(comboW)
  if ui.BeginCombo("##aacat", category or "All categories", ui.ComboFlags.HeightLarge) then
    if ui.Selectable("All categories", category == nil) then category = nil end
    for _, c in ipairs(cats) do
      if ui.Selectable(c, category == c) then category = c end
    end
    ui.EndCombo()
  end
  ui.SameLine()
  ui.SetNextItemWidth(comboW)
  if ui.BeginCombo("##aaview", VIEWS[view]) then
    for i, v in ipairs(VIEWS) do
      if ui.Selectable(v, view == i) then view = i end
    end
    ui.EndCombo()
  end
  W.Check(WINDOW, "CanPurchaseFilter", "Only what I can train now", CAN_PURCHASE)
end

local function accepts(_, texts)
  if needle ~= "" and not lowerOf(texts[1] or ""):find(needle, 1, true) then return false end
  if category and (texts[4] or "") ~= category then return false end
  local cur, max = ranks(texts)
  if view == 2 and cur <= 0 then return false end
  if view == 3 and cur > 0 then return false end
  if view == 4 and cur >= max then return false end
  return true
end

local opts = nil    -- this frame's window options (M.window)
local page = 0      -- this frame's tab
local hookRow = {}  -- the row as opts.row's functions see it (kept)

local function drawRow(_, texts, x0, y0, x1, y1, s, st)
  local cur, max = ranks(texts)
  local trainable = st.color[1] > 0.9 and st.color[2] > 0.9 -- the game greys what it will not train
  local maxed = max > 0 and cur >= max
  local hook = opts and opts.row
  local style = nil
  if hook then
    local r = hookRow
    r.name, r.rank, r.max, r.cost, r.category, r.trainable, r.maxed, r.page = texts[1] or "", math.max(cur, 0), max,
      texts[3] or "", texts[4] or "", trainable, maxed, page
    if hook.style then style = hook.style(r) end
    if style and style.ring then
      local c = style.ring
      ui.DrawRect(x0 + 1 * s, y0 + 1 * s, x1 - 1 * s, y1 - 1 * s, c[1], c[2], c[3], c[4] or 1, 6 * s, 1.5 * s)
    end
    if hook.width and hook.width > 0 then
      local px = x1 - hook.width * s
      if hook.draw then hook.draw(r, px, y0, x1, y1, s) end
      x1 = px
    end
  end
  local pad = 10 * s
  local right = 128 * s
  local nameW = x1 - x0 - pad * 2 - right
  local name = texts[1] or ""
  local tag = style and style.tag
  if tag then
    local tw = T.TextSize(tag[1], T.font.tiny) + 10 * s
    local fitted = T.Fit(name, nameW - tw - 6 * s, T.font.small)
    T.Text(x0 + pad, y0 + 4 * s, fitted, trainable and C.text or C.muted, T.font.small)
    local nx = x0 + pad + T.TextSize(fitted, T.font.small) + 6 * s
    local c = tag[2] or C.accent
    ui.DrawRectFilled(nx, y0 + 5 * s, nx + tw, y0 + 17 * s, c[1], c[2], c[3], 0.18, 4 * s)
    T.Text(nx + 5 * s, y0 + 5 * s, tag[1], c, T.font.tiny)
  else
    T.Text(x0 + pad, y0 + 4 * s, T.Fit(name, nameW, T.font.small), trainable and C.text or C.muted, T.font.small)
  end
  T.Text(x0 + pad, y0 + 20 * s, T.Fit(texts[4] or "", nameW, T.font.tiny), C.faint, T.font.tiny)
  -- rank pips and the next rank's cost
  local rx = x1 - right
  T.Text(rx, y0 + 4 * s, string.format("%d / %d", math.max(cur, 0), max), maxed and C.aa or C.muted, T.font.tiny)
  rankPips(rx, y0 + 25 * s, s, cur, max, 64 * s)
  local cost = texts[3] or ""
  if maxed then
    T.Text(x1 - pad, (y0 + y1) / 2 - 7 * s, "MAX", C.aa, T.font.tiny, "right")
  elseif cost ~= "" then
    local pw = T.TextSize("pt", T.font.tiny)
    T.Text(x1 - pad, (y0 + y1) / 2 - 5 * s, "pt", C.faint, T.font.tiny, "right")
    T.Text(x1 - pad - pw - 3 * s, (y0 + y1) / 2 - 8 * s, cost, trainable and C.gold or C.faint, T.font.small, "right")
  end
end

-- ---------------------------------------------------------------- the selected ability
local function details(x, y, w, h, s)
  local sel = splitKept("selected", W.Text(WINDOW, "ModernSelected"), "\t")
  ui.DrawRectFilled(x, y, x + w, y + h, C.ink[1], C.ink[2], C.ink[3], 0.025, 8 * s)
  local pad = 12 * s
  local ix, iw = x + pad, w - 2 * pad
  if not sel[1] or sel[1] == "" then
    ui.SetCursorScreenPos(x, y)
    P.Empty(w, h, "Select an ability", "Its rank, cost and requirements show here")
    return
  end
  local name, cur, max, cost = sel[2] or "", tonumber(sel[3]) or 0, tonumber(sel[4]) or 0, sel[5] or ""
  local yy = y + pad
  ui.PushClipRect(x, y, x + w, y + h, true)
  -- name, kind
  local reuse = splitKept("reuse", W.Text(WINDOW, "ModernReuse"), "\t")
  local activated = reuse[1] ~= nil and reuse[1] ~= ""
  T.Text(ix, yy, T.Fit(name, iw, T.font.normal), C.text, T.font.normal)
  local _, nh = T.TextSize("Ag", T.font.normal)
  yy = yy + nh + 4 * s
  local cw = P.Chip(ix, yy, s, activated and "Activated" or "Passive", activated and C.accent or C.muted)
  if cur >= max and max > 0 then P.Chip(ix + cw + 4 * s, yy, s, "Maxed", C.aa)
  elseif cur > 0 then P.Chip(ix + cw + 4 * s, yy, s, "Owned", C.pet) end
  yy = yy + 22 * s
  -- rank
  T.Text(ix, yy, "Rank", C.muted, T.font.small)
  T.Text(ix + iw, yy, string.format("%d of %d", math.max(cur, 0), max), C.text, T.font.small, "right")
  yy = yy + 18 * s
  T.Bar("aa.selrank", ix, yy, ix + iw, yy + 6 * s, s, max > 0 and math.max(cur, 0) / max or 0, C.aa, NO_TRAIL)
  yy = yy + 14 * s
  -- cost, level, reuse
  local level = W.Text(WINDOW, "ModernLevel")
  local _, levelMet = W.State(WINDOW, "ModernLevel")
  local tw = (iw - 8 * s) / 3
  P.Tile("##aacost", ix, yy, tw, 40 * s, s, "NEXT RANK", cost ~= "" and (cost .. " pt") or "-",
    { color = cost ~= "" and C.gold or C.faint, font = T.font.small })
  P.Tile("##aalevel", ix + tw + 4 * s, yy, tw, 40 * s, s, "LEVEL", level ~= "" and level or "-",
    { color = (level ~= "" and levelMet == false) and C.danger or C.text, font = T.font.small,
      tooltip = level ~= "" and (levelMet == false and "You are below the level this rank needs"
        or "The level this rank needs") or nil })
  local reuseText = activated and seconds(reuse[1]) or "-"
  local left = tonumber(reuse[2]) or 0
  P.Tile("##aareuse", ix + 2 * (tw + 4 * s), yy, tw, 40 * s, s, left > 0 and "READY IN" or "REUSE",
    left > 0 and seconds(left) or reuseText, { color = left > 0 and C.hpLow or C.text, font = T.font.small,
      tooltip = activated and ("Reuse time " .. reuseText) or "A passive ability" })
  yy = yy + 48 * s
  -- prerequisites
  local prereqs = W.Text(WINDOW, "ModernPrereqs")
  if prereqs ~= "" then
    T.Text(ix, yy, "Requires", C.muted, T.font.small)
    yy = yy + 18 * s
    for line in (prereqs .. "\n"):gmatch("(.-)\n") do
      local p = split(line, "\t")
      local met = p[3] == "1"
      local c = met and C.pet or C.danger
      ui.DrawCircleFilled(ix + 4 * s, yy + 7 * s, 3 * s, c[1], c[2], c[3], 1)
      T.Text(ix + 14 * s, yy, T.Fit(p[1] or "", iw - 70 * s, T.font.small), met and C.text or C.muted, T.font.small)
      T.Text(ix + iw, yy, "rank " .. (p[2] or "1"), met and C.faint or C.danger, T.font.tiny, "right")
      yy = yy + 18 * s
    end
    yy = yy + 4 * s
  end
  if opts and opts.details then
    local hookSel = { id = tonumber(sel[1]), name = name, rank = math.max(cur, 0), max = max, cost = cost }
    yy = yy + (opts.details(hookSel, ix, yy, iw, s) or 0)
  end
  ui.PopClipRect()
  -- the game's description, then the buttons
  local _, lh = ui.CalcTextSize("Ag")
  local btnH = lh + 10 * s
  local trainAll = eq.ChildShown(WINDOW, "TrainAllButton") ~= false
  local foot = btnH + 8 * s + (trainAll and (lh + 8 * s) or 0)
  ui.DrawLine(ix, yy, ix + iw, yy, C.ink[1], C.ink[2], C.ink[3], 0.06, 1)
  yy = yy + 6 * s
  local descH = math.max(30 * s, y + h - pad - foot - yy)
  ui.SetCursorScreenPos(ix, yy)
  ui.PushFont(T.font.small)
  W.Stml(WINDOW, "Description", iw, descH)
  ui.PopFont()
  ui.SetCursorScreenPos(ix, y + h - pad - foot + 4 * s)
  if trainAll then
    W.Check(WINDOW, "TrainAllButton", "Buy every rank I can",
      { tooltip = "Train buys all the ranks of this ability you can afford" })
  end
  ui.SetCursorScreenPos(ix, y + h - pad - btnH)
  ui.BeginGroup()
  local bw = (iw - 8 * s) / 2
  W.Button(WINDOW, "TrainButton", cost ~= "" and ("Train  (" .. cost .. " pt)") or "Train",
    { primary = true, width = bw, height = btnH, tooltip = "Buy the next rank of this ability" })
  ui.SameLine(0, 8 * s)
  W.Button(WINDOW, "HotButton", "Hotkey", { width = bw, height = btnH,
    tooltip = "Put this ability's hot button on the cursor (or drag its row)" })
  ui.EndGroup()
end

-- ---------------------------------------------------------------- the window
local TEST_CLASS = WINDOW .. ".class"
local function testClass() eq.SetTabPage(WINDOW, "Subwindows", 2) end

local DIALOG = {
  size = function() return 820, 560 end,
  resizable = { 420, 420 },
  keepTitle = true,
}

local ROWS = { rowH = 38, drag = true, filter = accepts, draw = drawRow }
local LIST_IDS = { [0] = "List1", "List2", "List3", "List4" }

local function body(x, y, w, h, s)
  header(x, y, w, s)
  if opts and opts.banner then
    local bx, by = ui.GetCursorScreenPos()
    local bh = opts.banner(bx, by, w, s) or 0
    ui.SetCursorScreenPos(bx, by + (bh > 0 and bh + 6 * s or 0))
  end
  page = W.Tabs(WINDOW, "Subwindows", PAGES)
  local list = LIST_IDS[page] or ("List" .. (page + 1))
  local rows = eq.WindowList(WINDOW, list) or {}
  local stacked = w < STACK_W * s
  local side = stacked and w or math.min(SIDE_W * s, w * 0.45)
  local cx, cy = ui.GetCursorScreenPos()
  local bottom = y + h
  local listW = stacked and w or (w - side - 10 * s)
  local listBottom = stacked and math.max(cy + (bottom - cy) * 0.4, bottom - 340 * s) or bottom
  -- filters and the list
  ui.BeginChild("##aaleft", listW, listBottom - cy, false)
  filters(s, rows)
  needle = search:lower()
  local _, lh = ui.CalcTextSize("Ag")
  ROWS.rows = rows -- (P.Rows reads the list itself where it does not take these)
  ROWS.reserve = opts and opts.row and opts.row.width or nil
  ROWS.empty = #rows == 0 and "No abilities on this tab" or "No ability matches"
  ROWS.emptySub = #rows == 0 and "" or "Clear the search or the filters to see more"
  local total, _, shown = P.Rows(WINDOW, list, 0, -(lh + 6 * s), ROWS)
  local fx, fy = ui.GetCursorScreenPos()
  local count = shown == total and string.format("%d abilities", total)
      or string.format("%d of %d abilities", shown, total)
  T.Text(fx, fy + 2 * s, count .. "   -   drag a row to make a hot button", C.faint, T.font.tiny)
  ui.EndChild()
  -- the details
  local dx, dy = stacked and cx or (cx + listW + 10 * s), stacked and (listBottom + 8 * s) or cy
  details(dx, dy, side, bottom - dy, s)
  ui.SetCursorScreenPos(x, y)
  ui.Dummy(w, h)
end

local M = {}

function M.window(o)
  if not eq.InGame() then return end
  opts = o
  -- testing: test.open.AAWindow.class=<s> shows the Class tab
  P.TestOpen(TEST_CLASS, testClass)
  hud.Dialog(WINDOW, "Alternate Advancement", DIALOG, body)
  opts = nil
end

return M
