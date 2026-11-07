-- Advanced Inventory (Hailstone's own window, InventoryManagerWnd: the EQ menu's "Advanced Inventory" and the
-- inventory window's button): everything the character has, worn, carried, banked and in the shared bank, in one
-- list or grid (eq.AllItems).
--   * search by name; filter by place, kind, mark and flags (usable by you, magic, lore, no trade, temporary,
--     stackable, sellable), each choice with how many items it would show; sort by place (grouped by bag), name,
--     kind (grouped), value, weight, quantity or level;
--   * Keep and Sell marks per item (every copy of it): a kept item is not destroyed, dropped, traded, given, sold or
--     sent by parcel (the client refuses: game/kept_items.h); Sell marked sells the carried ones to the open merchant
--     (eq.SellJunk). An item has one mark or the other. Marks for every item shown at once are in the ... menu;
--   * each item's icon is its slot: a click picks it up, drops or swaps (Shift: the stack, Ctrl: one), a right click
--     uses it, holding the right button shows its details, holding the left button (or dragging) puts a hot button
--     for it on the cursor, as in a bag. A banked item moves only while the bank is open. A right click on the row
--     opens its menu;
--   * along the bottom: what is shown and marked, and Stack / Sort / Tidy of the bags (lib/itemtools.lua).
local hud = dofile("lib/hud.lua")
local items = dofile("lib/items.lua")
local tools = dofile("lib/itemtools.lua")
local T = hud.T
local C = T.color
local WINDOW = "InventoryManagerWnd"

local W, H = 1040, 660
local MIN_W, MIN_H = 820, 460
local TOOL_H, FOOT_H = 42, 46
local SIDE_W = 196
local ROW_H, HEAD_H = 40, 28
local TILE, TILE_GAP = 46, 6
local SIDE_ROW = 25

local PLACES = { { "all", "Everything" }, { "worn", "Equipped" }, { "carried", "Carried" }, { "bank", "Bank" },
  { "shared", "Shared bank" } }
local PLACE_RANK = { worn = 1, carried = 2, bank = 3, shared = 4 }
local KINDS = { { "all", "All kinds" }, { "weapons", "Weapons" }, { "armor", "Armor and jewelry" },
  { "containers", "Containers" }, { "tradeskill", "Tradeskill" }, { "food", "Food and drink" },
  { "potions", "Potions and poisons" }, { "spells", "Spells and tomes" }, { "augments", "Augments" },
  { "other", "Other" } }
local KIND_NAME, KIND_RANK = {}, {}
for i, k in ipairs(KINDS) do KIND_NAME[k[1]], KIND_RANK[k[1]] = k[2], i end
local MARKS = { { "any", "Any" }, { "keep", "Keep" }, { "sell", "Sell" }, { "none", "Unmarked" } }
local FLAGS = { { "usable", "Usable by me" }, { "magic", "Magic" }, { "lore", "Lore" }, { "notrade", "No Trade" },
  { "temporary", "Temporary" }, { "stackable", "Stackable" }, { "sellable", "Sellable" } }
local SORTS = { "Place", "Name", "Kind", "Value", "Weight", "Quantity", "Level" }

-- ---------------------------------------------------------------- settings
local function setting(key, default)
  local v = ui.GetSetting("advinv." .. key)
  if v == nil or v == "" then return default end
  if type(default) == "boolean" then return v == "1" end
  return v
end
local function setSetting(key, v)
  ui.SetSetting("advinv." .. key, type(v) == "boolean" and (v and "1" or "0") or tostring(v))
end

local cfg = { flags = {} }
local function readSettings()
  cfg.place = setting("place", "all")
  cfg.kind = setting("kind", "all")
  cfg.mark = setting("mark", "any")
  cfg.sort = setting("sort", "Place")
  cfg.desc = setting("desc", false)
  cfg.view = setting("view", "list")
  for _, f in ipairs(FLAGS) do cfg.flags[f[1]] = setting("flag." .. f[1], false) end
end
local search = ""

-- ---------------------------------------------------------------- the items
local data = { rows = {}, inv = -1, keep = -1, at = -100, stamp = 0 }

local function kindOf(it)
  if (it.bagSlots or 0) > 0 then return "containers" end
  local c = it.category or ""
  if KIND_NAME[c] and c ~= "all" then return c end
  return "other"
end

-- Where a row is, for its line ("Backpack, slot 3") and its group header under the Place sort.
local function placeText(r)
  local n = r.slot + 1
  if r.where == "worn" then return eq.SlotName(r.slot) or "Worn" end
  if r.where == "carried" then
    if r.sub >= 0 then return (r.bag or "Bag") .. ", slot " .. (r.sub + 1) end
    return "General " .. (r.slot - 22)
  end
  local top = (r.where == "shared" and "Shared bank " or "Bank ") .. n
  if r.sub >= 0 then return top .. ": " .. (r.bag or "Bag") .. ", slot " .. (r.sub + 1) end
  return top
end

local function groupOf(r)
  if r.where == "worn" then return "worn", "Equipped" end
  local bagged = r.sub >= 0 or (r.item.bagSlots or 0) > 0
  if r.where == "carried" then
    if not bagged then return "carried", "Carried, loose" end
    return "c" .. r.slot, "General " .. (r.slot - 22) .. ": " .. (r.sub >= 0 and (r.bag or "Bag") or r.item.name)
  end
  local word = r.where == "shared" and "Shared bank" or "Bank"
  if not bagged then return r.where, word .. ", loose" end
  return r.where:sub(1, 1) .. r.slot, word .. " " .. (r.slot + 1) .. ": " .. (r.sub >= 0 and (r.bag or "Bag") or r.item.name)
end

local function refresh()
  local inv, keep = eq.ItemsRevision()
  local now = ui.GetTime()
  -- again when the inventory or the marks change, and each second (an item's charges, the bank's slots)
  if inv == data.inv and keep == data.keep and now - data.at < 1 and now >= data.at then return end
  data.inv, data.keep, data.at = inv, keep, now
  data.rows = eq.AllItems() or {}
  for i, r in ipairs(data.rows) do
    local it = r.item
    r._order = i
    r._lname = (it.name or ""):lower()
    r._kind = kindOf(it)
    r._place = placeText(r)
    r._group, r._groupText = groupOf(r)
    -- the place sort: worn, then loose carried items, then each bag (the bag, then what it holds), then the bank's
    local loose = r.sub < 0 and (it.bagSlots or 0) == 0
    r._placeKey = (PLACE_RANK[r.where] or 9) * 1000000 + (loose and 0 or 100000) + r.slot * 100 + (r.sub + 1)
    r._count = it.stackable and it.count or 1
    r._value = (it.value or 0) * r._count
    r._weight = (it.weight or 0) * r._count
    r._id = "##row" .. r.place
  end
  data.stamp = data.stamp + 1
end

local function markOf(r)
  if tools.Junk(r.item) then return "sell" end
  if tools.Kept(r.item) then return "keep" end
  return "none"
end

local FLAG_TEST = {
  usable = function(it) return it.usable end,
  magic = function(it) return it.magic end,
  lore = function(it) return it.lore end,
  notrade = function(it) return it.noTrade end,
  temporary = function(it) return it.temporary end,
  stackable = function(it) return it.stackable end,
  sellable = function(it) return it.sellable end,
}

-- The rows shown, sorted, with each one's group header ("" none), and how many each choice of the sidebar would show
-- with the other choices as they are: kept until the items, the marks, the search or a choice change.
local view = { rows = {}, groups = {}, counts = { place = {}, kind = {}, mark = {}, flag = {} } }
local function cacheKey()
  local parts = { data.stamp, tools.MarksStamp(), data.keep, search, cfg.place, cfg.kind, cfg.mark, cfg.sort,
    tostring(cfg.desc) }
  for _, f in ipairs(FLAGS) do parts[#parts + 1] = cfg.flags[f[1]] and "1" or "0" end
  return table.concat(parts, "|")
end

local sortKeys, sortDesc = {}, false
local function bySort(a, b)
  local ka, kb = sortKeys[a], sortKeys[b]
  if ka ~= kb then
    if sortDesc then return ka > kb end
    return ka < kb
  end
  if a._lname ~= b._lname then return a._lname < b._lname end
  return a._order < b._order
end

local function shownRows()
  local key = cacheKey()
  if view.key == key then return view end
  view.key = key
  local q = search:lower()
  local counts = { place = { all = 0 }, kind = { all = 0 }, mark = { any = 0 }, flag = {} }
  local rows = {}
  for _, r in ipairs(data.rows) do
    local it = r.item
    local m = markOf(r)
    local okSearch = q == "" or r._lname:find(q, 1, true) ~= nil
    local okPlace = cfg.place == "all" or r.where == cfg.place
    local okKind = cfg.kind == "all" or r._kind == cfg.kind
    local okMark = cfg.mark == "any" or m == cfg.mark
    local okFlags = true
    for _, f in ipairs(FLAGS) do
      if cfg.flags[f[1]] and not FLAG_TEST[f[1]](it) then okFlags = false end
    end
    if okSearch then
      if okKind and okMark and okFlags then
        counts.place[r.where] = (counts.place[r.where] or 0) + 1
        counts.place.all = counts.place.all + 1
      end
      if okPlace and okMark and okFlags then
        counts.kind[r._kind] = (counts.kind[r._kind] or 0) + 1
        counts.kind.all = counts.kind.all + 1
      end
      if okPlace and okKind and okFlags then
        counts.mark[m] = (counts.mark[m] or 0) + 1
        counts.mark.any = counts.mark.any + 1
      end
      if okPlace and okKind and okMark then
        -- a flag's count: what would show with it on, the other flags as they are
        for _, f in ipairs(FLAGS) do
          if FLAG_TEST[f[1]](it) then
            local rest = true
            for _, g in ipairs(FLAGS) do
              if g[1] ~= f[1] and cfg.flags[g[1]] and not FLAG_TEST[g[1]](it) then rest = false end
            end
            if rest then counts.flag[f[1]] = (counts.flag[f[1]] or 0) + 1 end
          end
        end
      end
    end
    if okSearch and okPlace and okKind and okMark and okFlags then rows[#rows + 1] = r end
  end
  local mode = cfg.sort
  sortKeys, sortDesc = {}, cfg.desc
  for _, r in ipairs(rows) do
    local k
    if mode == "Name" then k = r._lname
    elseif mode == "Kind" then k = KIND_RANK[r._kind] or 99
    elseif mode == "Value" then k = r._value
    elseif mode == "Weight" then k = r._weight
    elseif mode == "Quantity" then k = r._count
    elseif mode == "Level" then k = math.max(r.item.reqLevel or 0, r.item.recLevel or 0)
    else k = r._placeKey end
    sortKeys[r] = k
  end
  table.sort(rows, bySort)
  local groups = {}
  for i, r in ipairs(rows) do
    if mode == "Place" then groups[i] = r._groupText
    elseif mode == "Kind" then groups[i] = KIND_NAME[r._kind] or "Other"
    else groups[i] = "" end
  end
  view.rows, view.groups, view.counts = rows, groups, counts
  return view
end

-- ---------------------------------------------------------------- formats
local COINS = { { 1000, "p", "platinum" }, { 100, "g", "gold" }, { 10, "s", "silver" }, { 1, "c", "copper" } }
-- The two largest coins of a copper amount ("12p 4g"); "" for none.
local function money(cp)
  cp = math.floor(cp or 0)
  if cp <= 0 then return "", nil end
  local parts, top = {}, nil
  for _, c in ipairs(COINS) do
    local n = cp // c[1]
    if c[1] == 1000 then cp = cp % 1000 else cp = cp % c[1]; n = n % 10 end
    if n > 0 and #parts < 2 then
      parts[#parts + 1] = T.Thousands(n) .. c[2]
      top = top or c[3]
    end
  end
  return table.concat(parts, " "), top
end

local function weightText(w)
  if not w or w <= 0 then return "" end
  if w >= 100 then return string.format("%.0f", w) end
  return string.format("%.1f", w)
end

local function tags(it)
  local t = {}
  if it.magic then t[#t + 1] = "Magic" end
  if it.lore then t[#t + 1] = "Lore" end
  if it.noTrade then t[#t + 1] = "No Trade" end
  if it.temporary then t[#t + 1] = "Temporary" end
  if it.attuned then t[#t + 1] = "Attuned" end
  if (it.bagSlots or 0) > 0 then t[#t + 1] = it.bagSlots .. " slots" end
  if (it.reqLevel or 0) > 0 then t[#t + 1] = "Req " .. it.reqLevel
  elseif (it.recLevel or 0) > 0 then t[#t + 1] = "Rec " .. it.recLevel end
  if not it.usable and (it.equipSlots or 0) ~= 0 then t[#t + 1] = "Not usable" end
  return table.concat(t, "  \xC2\xB7  ")
end

-- ---------------------------------------------------------------- acting on an item
local function bankOpen() return eq.WindowShown("BankWnd") == true or eq.WindowShown("BigBankWnd") == true end
local function note(text) tools.state.note, tools.state.noteAt = text, ui.GetTime() end

-- Each banked place keeps one click handler (as lib/items.lua keeps one per carried slot).
local bankRecs = {}
local function bankRec(r)
  local rec = bankRecs[r.place]
  if rec then return rec end
  local shared = r.where == "shared"
  local sub = r.sub >= 0 and r.sub or nil
  rec = { id = "##advbank" .. r.place, place = shared and r.place or tools.Place("bank", r.slot, sub) }
  rec.onClick = function(button, outside)
    if tools.Intercept(rec.place, rec.item, button) then return end
    if not bankOpen() then
      if button == "left" or button == "right" or button == "rightup" then
        note("That is in your " .. (shared and "shared bank" or "bank") .. ": open the bank to move or use it.")
      end
      return
    end
    return eq.BankSlotClick(r.slot, sub, button, shared)
  end
  bankRecs[r.place] = rec
  return rec
end

-- The row's icon as its slot. Returns the item drawn.
local function slotFor(r, x, y, size, s)
  if r.where == "worn" or r.where == "carried" then
    return items.InvSlot(r.slot, r.sub >= 0 and r.sub or nil, x, y, size, s, nil) or r.item
  end
  local rec = bankRec(r)
  rec.item = r.item
  items.Slot(rec.id, x, y, size, s, r.item, nil, r._place, rec.onClick)
  tools.Marks(x, y, size, s, r.item, rec.place)
  if not bankOpen() then ui.DrawRectFilled(x, y, x + size, y + size, 0, 0, 0, 0.18, 6 * s) end
  return r.item
end

local function setMark(r, mark)
  if mark == "keep" then tools.SetKeep(r.item, not tools.Kept(r.item))
  elseif mark == "sell" then tools.SetSell(r.item, not tools.Junk(r.item)) end
end

-- ---------------------------------------------------------------- pieces
local function chip(id, x, y, label, on, s, tip)
  local tw = T.TextSize(label, T.font.small)
  local w = tw + 20 * s
  local clicked = T.Hit(id, x, y, x + w, y + 26 * s)
  local hov = ui.IsItemHovered()
  local a = C.accent
  if on then
    ui.DrawRectFilled(x, y, x + w, y + 26 * s, a[1], a[2], a[3], 0.24, 13 * s)
    ui.DrawRect(x, y, x + w, y + 26 * s, a[1], a[2], a[3], 0.75, 13 * s, 1)
  else
    ui.DrawRectFilled(x, y, x + w, y + 26 * s, C.ink[1], C.ink[2], C.ink[3], hov and 0.09 or 0.045, 13 * s)
  end
  local _, th = T.TextSize("Ag", T.font.small)
  T.Text(x + w / 2, y + (26 * s - th) / 2, label, (on or hov) and C.text or C.muted, T.font.small, "center")
  if hov and tip then ui.SetTooltip(tip) end
  return clicked, w
end

-- A mark's toggle on a row: a pill in the mark's colour while on, a faint outline otherwise.
local function markPill(id, x, y, w, h, s, label, on, color, enabled, tip)
  local clicked = T.Hit(id, x, y, x + w, y + h)
  local hov = ui.IsItemHovered()
  local c = color
  if on then
    ui.DrawRectFilled(x, y, x + w, y + h, c[1], c[2], c[3], 0.26, h / 2)
    ui.DrawRect(x, y, x + w, y + h, c[1], c[2], c[3], 0.85, h / 2, 1)
  elseif hov and enabled then
    ui.DrawRectFilled(x, y, x + w, y + h, c[1], c[2], c[3], 0.10, h / 2)
    ui.DrawRect(x, y, x + w, y + h, c[1], c[2], c[3], 0.5, h / 2, 1)
  else
    ui.DrawRect(x, y, x + w, y + h, C.ink[1], C.ink[2], C.ink[3], enabled and 0.12 or 0.05, h / 2, 1)
  end
  local _, th = T.TextSize("Ag", T.font.tiny)
  local col = on and C.text or (enabled and (hov and C.text or C.faint) or C.faint)
  T.Text(x + w / 2, y + (h - th) / 2, label, col, T.font.tiny, "center", enabled and 1 or 0.5)
  if hov and tip then ui.SetTooltip(tip) end
  return clicked and enabled
end

local function itemMenu(r)
  local it = r.item
  if it.magic then ui.TextColored(C.magic[1], C.magic[2], C.magic[3], 1, it.name) else ui.Text(it.name) end
  ui.TextDisabled(r._place)
  ui.Separator()
  if ui.MenuItem("Keep", tools.Kept(it)) then setMark(r, "keep") end
  if ui.IsItemHovered() then ui.SetTooltip("Never destroy, drop, trade, give, sell or send it") end
  if ui.MenuItem("Sell", tools.Junk(it), it.sellable == true) then setMark(r, "sell") end
  if ui.IsItemHovered() then ui.SetTooltip(it.sellable and "Sell marked sells it at a merchant" or "Merchants will not buy it") end
  ui.Separator()
  local banked = r.where == "bank" or r.where == "shared"
  local reach = not banked or bankOpen()
  if ui.MenuItem("Pick up", false, reach) then
    if banked then eq.BankSlotClick(r.slot, r.sub >= 0 and r.sub or nil, "left", r.where == "shared")
    else eq.ClickSlot(r.slot, r.sub >= 0 and r.sub or nil, "left") end
  end
  if ui.MenuItem((it.bagSlots or 0) > 0 and "Open" or "Use", false, reach) then
    if banked then eq.BankSlotClick(r.slot, r.sub >= 0 and r.sub or nil, "right", r.where == "shared")
    else eq.ClickSlot(r.slot, r.sub >= 0 and r.sub or nil, "right") end
  end
  if r.sub >= 0 and r.where ~= "shared" and ui.MenuItem("Open its bag", false, reach) then eq.PlaceClick(r.place, "open") end
  ui.Separator()
  if ui.MenuItem("Show only \"" .. T.Fit(it.name, 200, T.font.small) .. "\"") then search = it.name end
  if not reach then ui.TextDisabled("Open the bank to move or use it") end
end

-- ---------------------------------------------------------------- toolbar
local SORT_DESC, SORT_ASC = { tooltip = "Descending" }, { tooltip = "Ascending" }
local function toolbar(x, y, w, s, v)
  local wx, wy = ui.GetWindowPos()
  ui.SetCursorPos(x - wx, y - wy + 3 * s)
  ui.SetNextItemWidth(260 * s)
  ui.PushStyleVar(ui.StyleVar.FramePadding, 10 * s, 6 * s)
  ui.PushStyleVar(ui.StyleVar.FrameRounding, 14 * s)
  search = ui.InputTextWithHint("##advsearch", "Search your items", search, 64)
  ui.PopStyleVar(2)
  if search ~= "" then
    if T.Button("##advclear", x + 236 * s, y + 7 * s, x + 256 * s, y + 25 * s, s, "x", { tooltip = "Clear the search" }) then
      search = ""
    end
  end
  -- sort
  local cx = x + 274 * s
  local _, th = T.TextSize("Ag", T.font.small)
  T.Text(cx, y + 3 * s + (28 * s - th) / 2, "Sort", C.muted, T.font.small)
  ui.SetCursorPos(cx + 36 * s - wx, y - wy + 3 * s)
  ui.SetNextItemWidth(112 * s)
  ui.PushStyleVar(ui.StyleVar.FramePadding, 8 * s, 6 * s)
  if ui.BeginCombo("##advsort", cfg.sort) then
    for _, m in ipairs(SORTS) do
      if ui.Selectable(m, m == cfg.sort) then setSetting("sort", m) end
    end
    ui.EndCombo()
  end
  ui.PopStyleVar()
  if T.Button("##advdesc", cx + 154 * s, y + 3 * s, cx + 182 * s, y + 31 * s, s, cfg.desc and "v" or "^",
      cfg.desc and SORT_DESC or SORT_ASC) then
    setSetting("desc", not cfg.desc)
  end
  -- the view: list or grid
  local vx = cx + 196 * s
  local c1, w1 = chip("##advlist", vx, y + 4 * s, "List", cfg.view == "list", s, "Rows with each item's details")
  if c1 then setSetting("view", "list") end
  local c2 = chip("##advgrid", vx + w1 + 4 * s, y + 4 * s, "Grid", cfg.view == "grid", s, "Icons only, as in a bag")
  if c2 then setSetting("view", "grid") end
  -- the marks of everything shown
  local mx1 = x + w
  if T.Button("##advmore", mx1 - 30 * s, y + 3 * s, mx1, y + 31 * s, s, "...",
      { tooltip = "Mark everything shown" }) then ui.OpenPopup("##advbulk") end
  if ui.BeginPopup("##advbulk") then
    ui.TextDisabled(#v.rows == 1 and "The item shown" or ("The " .. #v.rows .. " items shown"))
    ui.Separator()
    if ui.MenuItem("Keep them all") then
      local seen = {}
      for _, r in ipairs(v.rows) do
        if not seen[r.item.id] and not tools.Kept(r.item) then tools.SetKeep(r.item, true) end
        seen[r.item.id] = true
      end
    end
    if ui.MenuItem("Mark them to sell") then
      local seen = {}
      for _, r in ipairs(v.rows) do -- not what is worn or kept, nor what no merchant buys
        if not seen[r.item.id] and r.where ~= "worn" and r.item.sellable and not tools.Kept(r.item) then
          tools.SetSell(r.item, true)
        end
        seen[r.item.id] = true
      end
    end
    if ui.IsItemHovered() then ui.SetTooltip("Leaves out what you wear, what is marked Keep and what merchants will not buy") end
    if ui.MenuItem("Clear their marks") then
      for _, r in ipairs(v.rows) do
        if tools.Kept(r.item) then tools.SetKeep(r.item, false) end
        if tools.Junk(r.item) then tools.SetSell(r.item, false) end
      end
    end
    ui.Separator()
    if ui.MenuItem("Clear every sell mark") then tools.ClearSell() end
    ui.EndPopup()
  end
  -- what the bags hold, right of the view
  local free, total = eq.InventorySpace()
  local tx = mx1 - 40 * s
  local low = free <= 5
  T.Text(tx, y + 3 * s + (28 * s - th) / 2, string.format("%d / %d free", free, total), low and C.danger or C.muted,
    T.font.small, "right")
end

-- ---------------------------------------------------------------- sidebar
local function sideHeading(x, y, w, text, s)
  T.Text(x + 8 * s, y + 8 * s, text, C.faint, T.font.tiny)
  return y + 26 * s
end

-- A choice of the sidebar; returns clicked and the next y.
local function sideRow(id, x, y, w, s, label, count, on, check)
  local h = SIDE_ROW * s
  local clicked = T.Hit(id, x, y, x + w, y + h)
  local hov = ui.IsItemHovered()
  local a = C.accent
  if on and not check then
    ui.DrawRectFilled(x, y, x + w, y + h, a[1], a[2], a[3], 0.18, 6 * s)
    ui.DrawRectFilled(x, y + 5 * s, x + 3 * s, y + h - 5 * s, a[1], a[2], a[3], 1, 1.5 * s)
  elseif hov then
    ui.DrawRectFilled(x, y, x + w, y + h, C.ink[1], C.ink[2], C.ink[3], 0.06, 6 * s)
  end
  local _, th = T.TextSize("Ag", T.font.small)
  local tx = x + 12 * s
  if check then -- a check box
    local bs = 13 * s
    local by = y + (h - bs) / 2
    if on then
      ui.DrawRectFilled(tx, by, tx + bs, by + bs, a[1], a[2], a[3], 0.9, 3 * s)
      ui.DrawLine(tx + 3 * s, by + 7 * s, tx + 5.5 * s, by + 10 * s, C.onAccent[1], C.onAccent[2], C.onAccent[3], 1, 1.8 * s)
      ui.DrawLine(tx + 5.5 * s, by + 10 * s, tx + 10.5 * s, by + 3.5 * s, C.onAccent[1], C.onAccent[2], C.onAccent[3], 1, 1.8 * s)
    else
      ui.DrawRect(tx, by, tx + bs, by + bs, C.frameBorder[1], C.frameBorder[2], C.frameBorder[3], hov and 1 or 0.7, 3 * s, 1)
    end
    tx = tx + bs + 8 * s
  end
  local muted = count == 0 and not on
  T.Text(tx, y + (h - th) / 2, T.Fit(label, w - (tx - x) - 40 * s, T.font.small),
    (on or hov) and C.text or (muted and C.faint or C.muted), T.font.small)
  if count then
    T.Text(x + w - 10 * s, y + (h - th) / 2, T.Thousands(count), on and C.text or C.faint, T.font.small, "right")
  end
  return clicked, y + h + 1 * s
end

local function filtersOn()
  if cfg.place ~= "all" or cfg.kind ~= "all" or cfg.mark ~= "any" then return true end
  for _, f in ipairs(FLAGS) do if cfg.flags[f[1]] then return true end end
  return false
end
local function resetFilters()
  setSetting("place", "all")
  setSetting("kind", "all")
  setSetting("mark", "any")
  for _, f in ipairs(FLAGS) do setSetting("flag." .. f[1], false) end
end

local function sidebar(x, y, w, h, s, v)
  local wx, wy = ui.GetWindowPos()
  ui.DrawRectFilled(x, y, x + w, y + h, C.ink[1], C.ink[2], C.ink[3], 0.03, 8 * s)
  ui.SetCursorPos(x - wx, y - wy)
  ui.BeginChild("##advside", w, h, false)
  local cx, cy = ui.GetCursorScreenPos()
  local cw = ui.GetContentRegionAvail()
  cx, cw = cx + 4 * s, cw - 8 * s
  local yy = cy + 2 * s
  local counts = v.counts
  yy = sideHeading(cx, yy, cw, "PLACE", s)
  for _, p in ipairs(PLACES) do
    local clicked
    clicked, yy = sideRow("##p" .. p[1], cx, yy, cw, s, p[2], counts.place[p[1]] or 0, cfg.place == p[1])
    if clicked then setSetting("place", p[1]) end
  end
  yy = sideHeading(cx, yy + 4 * s, cw, "MARK", s)
  for _, m in ipairs(MARKS) do
    local clicked
    clicked, yy = sideRow("##m" .. m[1], cx, yy, cw, s, m[2], counts.mark[m[1]] or 0, cfg.mark == m[1])
    if m[1] == "keep" or m[1] == "sell" then -- the mark's sign, left of its count
      local my = yy - 1 * s - SIDE_ROW * s
      if m[1] == "keep" then tools.Shield(cx + cw - 52 * s, my + (SIDE_ROW * s - 11 * s) / 2, 11 * s, C.good)
      else ui.DrawCircleFilled(cx + cw - 47 * s, my + SIDE_ROW * s / 2, 4 * s, C.danger[1], C.danger[2], C.danger[3], 1) end
    end
    if clicked then setSetting("mark", m[1]) end
  end
  yy = sideHeading(cx, yy + 4 * s, cw, "KIND", s)
  for _, k in ipairs(KINDS) do
    local n = counts.kind[k[1]] or 0
    if k[1] == "all" or n > 0 or cfg.kind == k[1] then -- kinds you have none of are left out
      local clicked
      clicked, yy = sideRow("##k" .. k[1], cx, yy, cw, s, k[2], n, cfg.kind == k[1])
      if clicked then setSetting("kind", k[1]) end
    end
  end
  yy = sideHeading(cx, yy + 4 * s, cw, "ONLY", s)
  for _, f in ipairs(FLAGS) do
    local on = cfg.flags[f[1]]
    local clicked
    clicked, yy = sideRow("##f" .. f[1], cx, yy, cw, s, f[2], counts.flag[f[1]] or 0, on, true)
    if clicked then setSetting("flag." .. f[1], not on) end
  end
  if filtersOn() then
    yy = yy + 8 * s
    if T.Button("##advreset", cx + 4 * s, yy, cx + cw - 4 * s, yy + 26 * s, s, "Reset filters") then resetFilters() end
    yy = yy + 30 * s
  end
  ui.SetCursorScreenPos(cx, yy)
  ui.Dummy(cw, 6 * s)
  ui.EndChild()
end

-- ---------------------------------------------------------------- the list and the grid
local COLS = { place = 0.40, qty = 0.665, value = 0.775, weight = 0.845 }
local MARK_W = 44
local KEEP_TIP = "Keep: never destroy, drop, trade, give, sell or send it (every copy of it)"
local SELL_TIP = "Sell: Sell marked sells it at a merchant (every copy you carry)"
local NO_SELL_TIP = "Merchants will not buy it"
local HELD_TIP = "This bag holds an item marked Keep"

local function emptyState(px, py, cw, s)
  local msg = #data.rows == 0 and "You have no items." or "No items match."
  T.Text(px + cw / 2, py + 40 * s, msg, C.muted, T.font.normal, "center")
  if #data.rows > 0 and (filtersOn() or search ~= "") then
    local bw = 130 * s
    if T.Button("##advempty", px + cw / 2 - bw / 2, py + 70 * s, px + cw / 2 + bw / 2, py + 96 * s, s, "Show everything") then
      resetFilters()
      search = ""
    end
  end
end

local function groupHeader(px, py, cw, s, text)
  T.Text(px + 6 * s, py + 9 * s, text:upper(), C.faint, T.font.tiny)
  ui.DrawLine(px, py + HEAD_H * s - 3 * s, px + cw, py + HEAD_H * s - 3 * s, C.ink[1], C.ink[2], C.ink[3], 0.08, 1)
end

local menuRow = nil

local function listRow(r, px, py, cw, s, th)
  local rh = ROW_H * s
  local it = r.item
  local kept, sell = tools.Kept(it), tools.Junk(it)
  local isz = rh - 8 * s
  local ix, iy = px + 8 * s, py + 3 * s
  local mw, mh = MARK_W * s, 20 * s
  local sx = px + cw - mw - 6 * s
  local kx = sx - mw - 6 * s
  T.Hit(r._id, px, py, px + cw, py + rh - 2 * s) -- (the row: its menu; the icon and the marks act on their own)
  local hov = ui.IsItemHovered()
  if hov and ui.IsMouseClicked(ui.Mouse.Right) then
    local mx = ui.GetMousePos()
    if mx > ix + isz and mx < kx then -- not on the icon (a right click there uses the item) nor the marks
      menuRow = r
      T.TakeRightClick()
    end
  end
  if hov then ui.DrawRectFilled(px, py, px + cw, py + rh - 2 * s, C.ink[1], C.ink[2], C.ink[3], 0.05, 6 * s) end
  -- the mark's bar at the left edge
  local bar = kept and C.good or (sell and C.danger or nil)
  if bar then ui.DrawRectFilled(px, py + 6 * s, px + 3 * s, py + rh - 8 * s, bar[1], bar[2], bar[3], 0.9, 1.5 * s) end
  slotFor(r, ix, iy, isz, s)
  local nx = ix + isz + 10 * s
  local colPlace = px + cw * COLS.place
  local nameW = colPlace - nx - 10 * s
  local name = it.name or ""
  local twoLine = py + 4 * s
  T.Text(nx, twoLine, T.Fit(name, nameW, T.font.small), it.magic and C.magic or C.text, T.font.small)
  local tg = tags(it)
  if tg ~= "" then T.Text(nx, twoLine + th + 1 * s, T.Fit(tg, nameW, T.font.tiny), C.faint, T.font.tiny) end
  local cy = py + (rh - 2 * s - th) / 2
  local banked = r.where == "bank" or r.where == "shared"
  T.Text(colPlace, cy, T.Fit(r._place, px + cw * COLS.qty - colPlace - 40 * s, T.font.small),
    banked and C.gold or (r.where == "worn" and C.accent or C.muted), T.font.small)
  if it.stackable then T.Text(px + cw * COLS.qty, cy, T.Thousands(it.count), C.text, T.font.small, "right") end
  local mv, coin = money(r._value)
  if mv ~= "" then T.Text(px + cw * COLS.value, cy, mv, C[coin] or C.gold, T.font.small, "right") end
  T.Text(px + cw * COLS.weight, cy, weightText(r._weight), C.faint, T.font.small, "right")
  -- the marks
  local my = py + (rh - 2 * s - mh) / 2
  local held = r.kept and not kept -- a bag holding a kept item
  if markPill("##k" .. r.place, kx, my, mw, mh, s, "Keep", kept, C.good, true, held and HELD_TIP or KEEP_TIP) then
    setMark(r, "keep")
  end
  if held then tools.Shield(kx + 5 * s, my + 5 * s, 10 * s, C.good, 0.6) end
  if markPill("##s" .. r.place, sx, my, mw, mh, s, "Sell", sell, C.danger, it.sellable == true,
      it.sellable and SELL_TIP or NO_SELL_TIP) then
    setMark(r, "sell")
  end
end

local function listView(v, x, y, w, h, s)
  local wx, wy = ui.GetWindowPos()
  ui.SetCursorPos(x - wx, y - wy)
  ui.BeginChild("##advitems", w, h, false)
  local px0, py0 = ui.GetCursorScreenPos()
  local cw = ui.GetContentRegionAvail()
  local _, top = ui.GetWindowPos()
  local bottom = top + h
  local _, th = T.TextSize("Ag", T.font.small)
  if #v.rows == 0 then emptyState(px0, py0, cw, s) end
  local yy = py0
  local header = nil
  local rh, hh = ROW_H * s, HEAD_H * s
  for i, r in ipairs(v.rows) do
    local g = v.groups[i]
    if g ~= "" and g ~= header then
      header = g
      if yy + hh >= top and yy <= bottom then groupHeader(px0, yy, cw, s, g) end
      yy = yy + hh
    end
    if yy + rh >= top and yy <= bottom then listRow(r, px0, yy, cw, s, th) end
    yy = yy + rh
  end
  ui.SetCursorScreenPos(px0, yy)
  ui.Dummy(cw, 6 * s)
  ui.EndChild()
end

local function gridView(v, x, y, w, h, s)
  local wx, wy = ui.GetWindowPos()
  ui.SetCursorPos(x - wx, y - wy)
  ui.BeginChild("##advgridview", w, h, false)
  local px0, py0 = ui.GetCursorScreenPos()
  local cw = ui.GetContentRegionAvail()
  local _, top = ui.GetWindowPos()
  local bottom = top + h
  if #v.rows == 0 then emptyState(px0, py0, cw, s) end
  local t, g = TILE * s, TILE_GAP * s
  local cols = math.max(1, math.floor((cw + g) / (t + g)))
  local yy, col = py0, 0
  local header = nil
  for i, r in ipairs(v.rows) do
    local gr = v.groups[i]
    if gr ~= "" and gr ~= header then
      if col > 0 then yy, col = yy + t + g, 0 end
      header = gr
      if yy + HEAD_H * s >= top and yy <= bottom then groupHeader(px0, yy, cw, s, gr) end
      yy = yy + HEAD_H * s
    end
    local tx = px0 + col * (t + g)
    if yy + t >= top and yy <= bottom then
      slotFor(r, tx, yy, t, s)
    end
    col = col + 1
    if col >= cols then yy, col = yy + t + g, 0 end
  end
  if col > 0 then yy = yy + t + g end
  ui.SetCursorScreenPos(px0, yy)
  ui.Dummy(cw, 6 * s)
  ui.EndChild()
end

local function columnHeader(x, y, w, s)
  local _, tth = T.TextSize("Ag", T.font.tiny)
  local function head(cx, text, align, tip)
    T.Text(cx, y, text, C.faint, T.font.tiny, align)
    if tip then
      local tw = T.TextSize(text, T.font.tiny)
      local x0 = align == "right" and cx - tw or cx
      T.Hit("##h" .. text, x0, y, x0 + tw, y + tth)
      if ui.IsItemHovered() then ui.SetTooltip(tip) end
    end
  end
  head(x + 8 * s, "ITEM")
  head(x + w * COLS.place, "WHERE")
  head(x + w * COLS.qty, "QTY", "right")
  head(x + w * COLS.value, "VALUE", "right", "The item's base value, for the whole stack; a merchant pays less")
  head(x + w * COLS.weight, "WEIGHT", "right")
  head(x + w - (MARK_W + 9) * s, "MARKS", "center")
end

-- ---------------------------------------------------------------- footer
local function footer(x, y, w, s, v)
  local _, th = T.TextSize("Ag", T.font.small)
  local banner = tools.ModeBanner("##advmode", x, x + 440 * s, y + 13 * s, s)
  local note = tools.Note()
  if banner == 0 and note then -- why the last Sort, Stack or sale did not start, for a few seconds
    T.Text(x, y + 15 * s, T.Fit(note, w * 0.5, T.font.small), C.danger, T.font.small)
  elseif banner == 0 then
    local keep, sell, sellCp = 0, 0, 0
    for _, r in ipairs(data.rows) do
      if tools.Kept(r.item) then keep = keep + 1 end
      if tools.Junk(r.item) and r.where == "carried" and r.item.sellable then
        sell = sell + 1
        sellCp = sellCp + r._value
      end
    end
    local shown = #v.rows == #data.rows and string.format("%d items", #data.rows)
      or string.format("%d of %d items", #v.rows, #data.rows)
    local tx = x
    tx = tx + T.Text(tx, y + 15 * s, shown, C.muted, T.font.small) + 18 * s
    tools.Shield(tx, y + 15 * s + (th - 11 * s) / 2, 11 * s, C.good)
    tx = tx + 14 * s + T.Text(tx + 14 * s, y + 15 * s, keep .. " kept", C.muted, T.font.small) + 18 * s
    ui.DrawCircleFilled(tx + 4 * s, y + 15 * s + th / 2, 4 * s, C.danger[1], C.danger[2], C.danger[3], 1)
    local mv = money(sellCp)
    T.Text(tx + 14 * s, y + 15 * s, sell .. " to sell" .. (mv ~= "" and (" (worth " .. mv .. ")") or ""), C.muted, T.font.small)
  end
  -- right: Sell marked at an open merchant, then Stack / Sort / ...
  local x1 = x + w
  local tw = tools.ToolbarWidth(s, true)
  local sellW = 130 * s
  local sx1 = x1 - tw - 10 * s
  local merchant = eq.WindowShown("MerchantWnd") == true
  local n = tools.JunkCarried()
  local busy = eq.ItemJob()
  if not busy then
    local tip = tools.Note() or (not merchant and "Open a merchant to sell what you marked"
      or (n > 0 and "Sell every item you carry that is marked to sell" or "Mark items to sell first"))
    if T.Button("##advsell", sx1 - sellW, y + 10 * s, sx1, y + 36 * s, s,
        n > 0 and ("Sell marked (" .. n .. ")") or "Sell marked",
        { enabled = merchant and n > 0, primary = merchant and n > 0, tooltip = tip }) then
      tools.SellJunk()
    end
  end
  tools.Toolbar("##advtools", x1, y + 11 * s, s, "inventory", nil, true)
end

-- ---------------------------------------------------------------- window
local DIALOG = {
  size = function() return W, H end,
  place = function(sw, sh, w, h) return sw / 2 - w / 2, sh / 2 - h / 2 - 20 end,
  resizable = { MIN_W, MIN_H },
  keepTitle = true,
}

local function body(x, y, w, h, s)
  refresh()
  readSettings()
  menuRow = nil
  local v = shownRows()
  toolbar(x, y, w, s, v)
  local by = y + TOOL_H * s
  local bodyH = h - TOOL_H * s - FOOT_H * s
  sidebar(x, by, SIDE_W * s, bodyH, s, v)
  local lx = x + SIDE_W * s + 14 * s
  local lw = x + w - lx
  if cfg.view == "grid" then
    gridView(v, lx, by, lw, bodyH, s)
  else
    columnHeader(lx, by, lw - 14 * s, s)
    listView(v, lx, by + 18 * s, lw, bodyH - 18 * s, s)
  end
  if menuRow then
    view.menu = menuRow
    ui.OpenPopup("##advitemmenu")
  end
  if ui.BeginPopup("##advitemmenu") then
    if view.menu then itemMenu(view.menu) end
    ui.EndPopup()
  end
  local fy = y + h - FOOT_H * s
  ui.DrawLine(x, fy, x + w, fy, C.ink[1], C.ink[2], C.ink[3], 0.06, 1)
  footer(x, fy, w, s, v)
end

ui.window("Advanced Inventory", function()
  if not eq.InGame() then return end
  hud.Dialog(WINDOW, "Advanced Inventory", DIALOG, body)
end)
