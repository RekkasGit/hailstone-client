-- Bags: replace the game's container windows (Bag1 .. Bag36), one window per open bag, titled with the bag's name,
-- and the open tradeskill object in the zone (WorldContainer: a forge, an oven) when it has its own window (opened
-- that way, or by the tradeskill window's Experiment).
-- The slots are the bag window's own (eq.BagSlotItem / eq.BagSlotClick: pick up, drop, swap, split, sell, inspect),
-- in rows of up to five. A tradeskill container has its Combine button over the slots. Over them (Hailstone): the
-- bag's filter, Stack and Sort for this bag (lib/itemtools.lua); slots the inventory's or bank's search does not match
-- are dimmed, locked slots and junk marked, and while a marking mode is on a click marks instead.
local hud = dofile("lib/hud.lua")
local items = dofile("lib/items.lua")
local tools = dofile("lib/itemtools.lua")
local T = hud.T

local SLOT, GAP = 44, 5

local function perRow(count)
  if count <= 5 then return count end
  return math.min(5, math.ceil(count / 2))
end

-- One record per bag window, made once: its dialog's opts and body read what bag() last read this frame.
local bags = {}
local PRIMARY = { primary = true }
local HELD = { held = true }

local function record(window, n)
  local b = { window = window }
  local function slotName(i) return b.name .. ", slot " .. (i + 1) end
  b.opts = {
    size = function()
      return math.max(b.cols * (SLOT + GAP) - GAP, b.sortable and 200 or 150), b.top + b.rows * (SLOT + GAP) - GAP
    end,
    -- the first bags open in a column at the right, above the menu button
    place = function(sw, sh, w, h, s)
      if not n then return (sw - w) / 2, (sh - h) / 2 end
      local k = (n - 1) % 6
      return sw - w - 24 * s - ((n - 1) // 6) * (w + 10 * s), sh - 90 * s - h - k * (h + 40 * s)
    end,
    onClose = function() eq.ClickChild(window, "StandardDoneButton", "left") end, -- closes the bag in game
  }
  b.body = function(x, y, w, h, s)
    local combine, sortable, where, bagSlot, cols = b.combine, b.sortable, b.where, b.bagSlot, b.cols
    if combine then
      local bw = (w - 6 * s) / 2
      if T.Button("##combine", x, y, x + bw, y + 26 * s, s, "Combine", PRIMARY) then
        eq.ClickChild(window, "Container_Combine", "left")
      end
      if T.Button("##done", x + bw + 6 * s, y, x + w, y + 26 * s, s, "Done") then
        eq.ClickChild(window, "CombineDoneButton", "left")
      end
    end
    if sortable then
      local ty = y + (combine and 34 or 0) * s
      local fw = tools.FilterChip(b.id, x, ty, s, tools.Place(where, bagSlot))
      tools.Toolbar(b.id, x + w, ty, s, window, x + fw + 6 * s)
    end
    local sz = SLOT * s
    for i = 0, b.count - 1 do
      local cx = x + (i % cols) * (SLOT + GAP) * s
      local cy = y + b.top * s + (i // cols) * (SLOT + GAP) * s
      local item = eq.BagSlotItem(window, i)
      local place = sortable and tools.Place(where, bagSlot, i) or nil
      items.Slot("##slot" .. i, cx, cy, sz, s, item, nil, slotName(i), function(button, outside)
        if place and tools.Intercept(place, item, button) then return end
        return eq.BagSlotClick(window, i, button, outside)
      end, HELD)
      if item then tools.Mark(cx, cy, sz, s, item, where, nil, place)
      elseif place then tools.Marks(cx, cy, sz, s, nil, place) end
    end
  end
  b.id = "##" .. window
  return b
end

-- n: the bag's place in the column at the right; nil: the middle of the screen
local function bag(window, n)
  -- Only open bag windows are read: the manager keeps closed ones around with whatever they last held.
  if not eq.WindowShown(window) then return end
  local name, count, combine, where, bagSlot = eq.BagInfo(window)
  if not name or not count or count < 1 then return end
  local b = bags[window]
  if not b then
    b = record(window, n)
    bags[window] = b
  end
  local cols = perRow(count)
  b.name, b.count, b.combine, b.where, b.bagSlot = name, count, combine, where, bagSlot
  b.cols, b.rows = cols, math.ceil(count / cols)
  b.sortable = where == "inventory" or where == "bank"
  b.top = (combine and 34 or 0) + (b.sortable and 30 or 0)
  hud.Dialog(window, name, b.opts, b.body)
end

local BAG_WINDOWS = {}
for i = 1, 36 do BAG_WINDOWS[i] = "Bag" .. i end

ui.window("Bags", function()
  if not eq.InGame() then return end
  tools.WatchLoot()
  for i = 1, 36 do bag(BAG_WINDOWS[i], i) end
  bag("WorldContainer")
end)
