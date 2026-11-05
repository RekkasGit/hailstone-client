-- Point merchant: replaces the game's PointMerchantWnd (a merchant who sells for an alternate currency): the
-- merchant and how much of the currency you hold (with its icon), the goods as a sortable table with a search, a
-- cost above what you hold in red, Purchase, Sell (the item on your cursor; its offer on hover) and Done
-- (lib/markets_points.lua). The list and buttons are the original's children.
local hud = dofile("lib/hud.lua")
local M = dofile("lib/markets.lua")
local P = dofile("lib/markets_points.lua")
local WINDOW = "PointMerchantWnd"

-- The merchant's currency: the point list's one row names it ("Currency <n>", as eq.AltCurrencies does).
local function currency()
  local row = eq.ListRow(WINDOW, "PointMerchant_PointList", 0)
  return row and row[1] or ""
end
local function icon()
  local name = currency()
  for _, c in ipairs(eq.AltCurrencies(true)) do
    if c.name == name and c.icon and c.icon > 0 then return eq.ItemIcon(c.icon) end
  end
  return nil
end

local CFG = { window = WINDOW, prefix = "PointMerchant_", currency = currency, icon = icon }

local OPTS = {
  size = function() return 420, 460 end,
  resizable = { 320, 300 },
}
local function body(x, y, w, h, s) P.Draw(CFG, w, h, s) end

ui.window("Point Merchant", function()
  if not eq.InGame() then return end
  OPTS.shown = M.Forced(WINDOW)
  hud.Dialog(WINDOW, "Point Merchant", OPTS, body)
end)
