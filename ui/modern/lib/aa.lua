-- Alternate Advancement: replaces the game's AAWindow. A header with the points to spend, the points spent, the AA
-- experience and the share of experience that goes to it (- / +); one list of every ability on the game's four tabs
-- (General, Archetype, Class, Special: AAWindow's ModernList, app/lua_eq_progress.cpp), with a search and filters over
-- all of it (the tab, the category, owned / not owned and the game's own "trainable only"), and columns for the tab,
-- the era (the expansion the ability came with), activated or passive, the rank and the next rank's cost; and the
-- selected ability at the side: its rank, the next rank's cost, the level and the abilities it needs, its reuse
-- time, the game's description, Train (and "all ranks" where the server allows it) and Hotkey. A deliberate
-- difference: the stock window shows one tab's list at a time. Rows are the game's (eq.ListClick selects the
-- ability, so Train and Hotkey act on it; dragging a row puts the ability's hot button on the cursor, as on the stock
-- list); an ability greyed is one the game will not train now. The requirements come from the window's Modern*
-- children.
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
-- would train it now), maxed, page (its tab, 0-based). sel, the selected ability: id (its rank's), name, rank, max,
-- cost (text).
local hud = dofile("lib/hud.lua")
local W = dofile("lib/widgets.lua")
local P = dofile("lib/progress.lua")
local T = hud.T
local C = T.color
local WINDOW = "AAWindow"
local TABS = { "General", "Archetype", "Class", "Special" } -- the stock tabs (the ability's type 1..4)
local TAB_KEYS = { "1", "2", "3", "4" }                      -- ModernList's tab column
local VIEWS = { "All abilities", "Owned", "Not owned", "Not maxed" }
local SIDE_W = 260      -- the details' width beside the list (design pixels)
local STACK_W = 600     -- narrower than this, the details go under the list
local LIST = "ModernList"

local search, view, category, tab = "", 1, nil, nil -- category: the Category column's text, tab: 1..4, nil any
local needle = ""                                     -- the search in lower case, this frame

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

-- ---------------------------------------------------------------- the header
local POINTS_TILE = { color = C.gold, font = T.font.large, tooltip = "Points you can spend" }
local SPENT_TILE = {}
local LESS_BUTTON = { tooltip = "Less of your experience goes to AA" }
local MORE_BUTTON = { tooltip = "More of your experience goes to AA" }
local assignedText, spentTip = nil, nil

local function header(x, y, w, s)
  local h = 36 * s
  local tileW = 80 * s
  local points = W.Text(WINDOW, "CurrentCount")
  P.Tile("##aapts", x, y, tileW, h, s, "UNSPENT", points ~= "" and points or "0", POINTS_TILE)
  local assigned = W.Text(WINDOW, "AssignedCount")
  if assigned ~= assignedText then
    assignedText, spentTip = assigned, "Points spent on abilities (assigned: " .. assigned .. ")"
  end
  SPENT_TILE.tooltip = spentTip
  P.Tile("##aaspent", x + tileW + 6 * s, y, tileW, h, s, "SPENT", W.Text(WINDOW, "TotalCount"), SPENT_TILE)
  -- the AA experience, and at its right the share of experience going to it
  local gx = x + 2 * tileW + 16 * s
  local gw = w - (gx - x)
  local frac, text = eq.Gauge(5)
  T.Text(gx, y + 3 * s, "AA experience", C.muted, T.font.small)
  local ew = T.TextSize("AA experience", T.font.small)
  T.Text(gx + ew + 8 * s, y + 3 * s, frac and string.format("%.1f%%", frac * 100) or (text or ""), C.text,
    T.font.small)
  local bw, pctW = 22 * s, 42 * s
  local bx = gx + gw - (2 * bw + pctW)
  T.Text(bx - 6 * s, y + 3 * s, "To AA", C.faint, T.font.small, "right")
  ui.SetCursorScreenPos(bx, y + 1 * s)
  LESS_BUTTON.width, LESS_BUTTON.height = bw, 18 * s
  W.Button(WINDOW, "LessExpButton", "-", LESS_BUTTON)
  T.Text(bx + bw + pctW / 2, y + 3 * s, W.Text(WINDOW, "ExpCount"), C.aa, T.font.small, "center")
  ui.SetCursorScreenPos(bx + bw + pctW, y + 1 * s)
  MORE_BUTTON.width, MORE_BUTTON.height = bw, 18 * s
  W.Button(WINDOW, "MoreExpButton", "+", MORE_BUTTON)
  T.Bar("aa.exp", gx, y + 25 * s, gx + gw, y + 31 * s, s, frac or 0, C.aa, NO_TRAIL)
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
local CAN_PURCHASE_LABEL = "Can train now"

local function combo(id, w, current, items, none)
  ui.SetNextItemWidth(w)
  local picked = current
  if ui.BeginCombo(id, current and items[current] or none, ui.ComboFlags.HeightLarge) then
    if none and ui.Selectable(none, current == nil) then picked = nil end
    for i, v in ipairs(items) do
      if ui.Selectable(v, current == i) then picked = i end
    end
    ui.EndCombo()
  end
  return picked
end

-- The search and the filters, across the window over every tab's abilities.
local catIndex = {}
local function filters(w, s, rows)
  local seen, cats = categories(rows)
  if category and not seen[category] then category = nil end
  local checkW = T.TextSize(CAN_PURCHASE_LABEL, T.font.small) + 30 * s
  local gap = 6 * s
  local tabW, catW, viewW = math.min(100 * s, w * 0.16), math.min(130 * s, w * 0.2), math.min(110 * s, w * 0.17)
  local searchW = w - tabW - catW - viewW - checkW - 4 * gap
  local wrap = searchW < 120 * s -- too narrow for it all: the check box on a line of its own
  if wrap then searchW = searchW + checkW + gap end
  search = P.Search("##aasearch", search, "Search abilities", searchW)
  ui.SameLine(0, gap)
  tab = combo("##aatab", tabW, tab, TABS, "All tabs")
  ui.SameLine(0, gap)
  for i = #catIndex, 1, -1 do catIndex[i] = nil end
  local ci = nil
  for i, c in ipairs(cats) do
    catIndex[i] = c
    if c == category then ci = i end
  end
  ci = combo("##aacat", catW, ci, catIndex, "All categories")
  category = ci and catIndex[ci] or nil
  ui.SameLine(0, gap)
  view = combo("##aaview", viewW, view, VIEWS) or view
  if not wrap then ui.SameLine(0, gap) end
  W.Check(WINDOW, "CanPurchaseFilter", CAN_PURCHASE_LABEL, CAN_PURCHASE)
end

local function accepts(_, texts)
  if tab and texts[5] ~= TAB_KEYS[tab] then return false end
  if needle ~= "" and not lowerOf(texts[1] or ""):find(needle, 1, true) then return false end
  if category and (texts[4] or "") ~= category then return false end
  local cur, max = ranks(texts)
  if view == 2 and cur <= 0 then return false end
  if view == 3 and cur > 0 then return false end
  if view == 4 and cur >= max then return false end
  return true
end

local opts = nil    -- this frame's window options (M.window)
local hookRow = {}  -- the row as opts.row's functions see it (kept)

-- The columns of a row from x0 to x1 (the script's part already taken off), right to left: the cost, the rank,
-- activated or passive, the era, the tab and the category; the name has what is left. Narrow, the category goes
-- first, then the tab. (Kept: the header and every row read the same table.)
local col = {}
local function columns(x0, x1, s)
  local pad = 8 * s
  local x = x1 - pad
  col.cost = x
  x = x - 34 * s
  col.rank1, col.rank0 = x, x - 40 * s
  x = col.rank0 - 6 * s
  col.kind1, col.kind0 = x, x - 46 * s
  x = col.kind0 - 6 * s
  col.era1, col.era0 = x, x - 96 * s
  x = col.era0 - 6 * s
  local room = x - (x0 + pad)
  col.tab0, col.cat0 = nil, nil
  if room - 66 * s >= 130 * s then
    col.tab0, col.tab1 = x - 60 * s, x
    x = col.tab0 - 6 * s
    room = x - (x0 + pad)
    if room - 106 * s >= 150 * s then
      col.cat0, col.cat1 = x - 100 * s, x
      x = col.cat0 - 6 * s
    end
  end
  col.name0, col.name1 = x0 + pad, x
  return col
end

local function columnHeads(x0, y, x1, s)
  local c = columns(x0, x1, s)
  local f, k = T.font.tiny, C.faint
  T.Text(c.name0, y, "ABILITY", k, f)
  if c.cat0 then T.Text(c.cat0, y, "CATEGORY", k, f) end
  if c.tab0 then T.Text(c.tab0, y, "TAB", k, f) end
  T.Text(c.era0, y, "ERA", k, f)
  T.Text(c.kind0, y, "TYPE", k, f)
  T.Text((c.rank0 + c.rank1) / 2, y, "RANK", k, f, "center")
  T.Text(c.cost, y, "COST", k, f, "right")
end

local function drawRow(_, texts, x0, y0, x1, y1, s, st)
  local cur, max = ranks(texts)
  local trainable = st.color[1] > 0.9 and st.color[2] > 0.9 -- the game greys what it will not train
  local maxed = max > 0 and cur >= max
  local page = (tonumber(texts[5]) or 1) - 1
  local hook = opts and opts.row
  local style = nil
  if hook then
    local r = hookRow
    r.name, r.rank, r.max, r.cost, r.category, r.trainable, r.maxed, r.page = texts[1] or "", math.max(cur, 0), max,
      texts[3] or "", texts[4] or "", trainable, maxed, page
    if hook.style then style = hook.style(r) end
    if style and style.ring then
      local c = style.ring
      ui.DrawRect(x0 + 1 * s, y0 + 1 * s, x1 - 1 * s, y1 - 1 * s, c[1], c[2], c[3], c[4] or 1, 5 * s, 1.5 * s)
    end
    if hook.width and hook.width > 0 then
      local px = x1 - hook.width * s
      if hook.draw then hook.draw(r, px, y0, x1, y1, s) end
      x1 = px
    end
  end
  local c = columns(x0, x1, s)
  local _, th = T.TextSize("Ag", T.font.small)
  local _, tt = T.TextSize("Ag", T.font.tiny)
  local ty = (y0 + y1 - th) / 2
  local tiny = (y0 + y1 - tt) / 2
  local nameW = c.name1 - c.name0
  local name = texts[1] or ""
  local tag = style and style.tag
  if tag then
    local tw = T.TextSize(tag[1], T.font.tiny) + 10 * s
    local fitted = T.Fit(name, nameW - tw - 6 * s, T.font.small)
    T.Text(c.name0, ty, fitted, trainable and C.text or C.muted, T.font.small)
    local nx = c.name0 + T.TextSize(fitted, T.font.small) + 6 * s
    local tc = tag[2] or C.accent
    ui.DrawRectFilled(nx, tiny - 1 * s, nx + tw, tiny + tt + 1 * s, tc[1], tc[2], tc[3], 0.18, 4 * s)
    T.Text(nx + 5 * s, tiny, tag[1], tc, T.font.tiny)
  else
    T.Text(c.name0, ty, T.Fit(name, nameW, T.font.small), trainable and C.text or C.muted, T.font.small)
  end
  if c.cat0 then T.Text(c.cat0, tiny, T.Fit(texts[4] or "", c.cat1 - c.cat0, T.font.tiny), C.faint, T.font.tiny) end
  if c.tab0 then T.Text(c.tab0, tiny, TABS[page + 1] or "", C.muted, T.font.tiny) end
  local era = texts[6] or ""
  T.Text(c.era0, tiny, era ~= "" and T.Fit(era, c.era1 - c.era0, T.font.tiny) or "-", C.faint, T.font.tiny)
  local active = texts[7] == "1"
  T.Text(c.kind0, tiny, active and "Active" or "Passive", active and C.accent or C.faint, T.font.tiny)
  T.Text((c.rank0 + c.rank1) / 2, tiny, string.format("%d/%d", math.max(cur, 0), max), maxed and C.aa or C.muted,
    T.font.tiny, "center")
  local cost = texts[3] or ""
  if maxed then
    T.Text(c.cost, tiny, "MAX", C.aa, T.font.tiny, "right")
  elseif cost ~= "" then
    T.Text(c.cost, ty, cost, trainable and C.gold or C.faint, T.font.small, "right")
  end
end

-- ---------------------------------------------------------------- the selected ability
local function details(x, y, w, h, s)
  local sel = splitKept("selected", W.Text(WINDOW, "ModernSelected"), "\t")
  ui.DrawRectFilled(x, y, x + w, y + h, C.ink[1], C.ink[2], C.ink[3], 0.025, 8 * s)
  local pad = 10 * s
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
  P.Tile("##aacost", ix, yy, tw, 36 * s, s, "NEXT RANK", cost ~= "" and (cost .. " pt") or "-",
    { color = cost ~= "" and C.gold or C.faint, font = T.font.small })
  P.Tile("##aalevel", ix + tw + 4 * s, yy, tw, 36 * s, s, "LEVEL", level ~= "" and level or "-",
    { color = (level ~= "" and levelMet == false) and C.danger or C.text, font = T.font.small,
      tooltip = level ~= "" and (levelMet == false and "You are below the level this rank needs"
        or "The level this rank needs") or nil })
  local reuseText = activated and seconds(reuse[1]) or "-"
  local left = tonumber(reuse[2]) or 0
  P.Tile("##aareuse", ix + 2 * (tw + 4 * s), yy, tw, 36 * s, s, left > 0 and "READY IN" or "REUSE",
    left > 0 and seconds(left) or reuseText, { color = left > 0 and C.hpLow or C.text, font = T.font.small,
      tooltip = activated and ("Reuse time " .. reuseText) or "A passive ability" })
  yy = yy + 42 * s
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
local function testClass() tab = 3 end

local DIALOG = {
  size = function() return 840, 500 end,
  resizable = { 420, 400 },
  keepTitle = true,
}

local ROWS = { rowH = 24, drag = true, filter = accepts, draw = drawRow }

local function body(x, y, w, h, s)
  header(x, y, w, s)
  if opts and opts.banner then
    local bx, by = ui.GetCursorScreenPos()
    local bh = opts.banner(bx, by, w, s) or 0
    ui.SetCursorScreenPos(bx, by + (bh > 0 and bh + 6 * s or 0))
  end
  local rows = eq.WindowList(WINDOW, LIST) or {}
  filters(w, s, rows)
  needle = search:lower()
  local stacked = w < STACK_W * s
  local side = stacked and w or math.min(SIDE_W * s, w * 0.4)
  local cx, cy = ui.GetCursorScreenPos()
  cy = cy + 2 * s
  local bottom = y + h
  local listW = stacked and w or (w - side - 8 * s)
  local listBottom = stacked and math.max(cy + (bottom - cy) * 0.4, bottom - 320 * s) or bottom
  -- the column heads and the list
  ui.SetCursorScreenPos(cx, cy)
  ui.BeginChild("##aaleft", listW, listBottom - cy, false)
  local lx, ly = ui.GetCursorScreenPos()
  local reserve = opts and opts.row and opts.row.width or nil
  local innerW = ROWS.innerW or listW -- (the rows' width last frame: less the scroll bar when it shows)
  local _, hh = T.TextSize("Ag", T.font.tiny)
  columnHeads(lx, ly, lx + innerW - (reserve or 0) * s, s)
  ui.SetCursorScreenPos(lx, ly + hh + 4 * s)
  local _, lh = T.TextSize("Ag", T.font.tiny)
  ROWS.rows = rows -- (P.Rows reads the list itself where it does not take these)
  ROWS.reserve = reserve
  ROWS.empty = #rows == 0 and "No abilities" or "No ability matches"
  ROWS.emptySub = #rows == 0 and "" or "Clear the search or the filters to see more"
  local total, _, shown = P.Rows(WINDOW, LIST, 0, -(lh + 4 * s), ROWS)
  local fx, fy = ui.GetCursorScreenPos()
  local count = shown == total and string.format("%d abilities", total)
      or string.format("%d of %d abilities", shown, total)
  T.Text(fx, fy + 2 * s, count .. "   -   drag a row to make a hot button", C.faint, T.font.tiny)
  ui.EndChild()
  -- the details
  local dx, dy = stacked and cx or (cx + listW + 8 * s), stacked and (listBottom + 6 * s) or cy
  details(dx, dy, side, bottom - dy, s)
  ui.SetCursorScreenPos(x, y)
  ui.Dummy(w, h)
end

local M = {}

function M.window(o)
  if not eq.InGame() then return end
  opts = o
  -- testing: test.open.AAWindow.class=<s> shows the Class tab's abilities
  P.TestOpen(TEST_CLASS, testClass)
  hud.Dialog(WINDOW, "Alternate Advancement", DIALOG, body)
  opts = nil
end

return M
