-- Adventure record: replaces the game's AdventureStatsWnd. Your totals as tiles (points to spend, wins, losses,
-- the win rate, points earned: the theme list's "All Themes" row), then each theme as a sortable table (wins,
-- losses, rank, rank percentile, points), and Refresh / Leaderboard. The numbers are the original's
-- (AdvStats_ThemeList, AdvStats_AvailableValue) and the buttons its own; "-" until the server has sent them.
local hud = dofile("lib/hud.lua")
local W = dofile("lib/widgets.lua")
local K = dofile("lib/quests_kit.lua")
local T = hud.T
local C = T.color
local WINDOW = "AdventureStatsWnd"
local LIST = "AdvStats_ThemeList"

local WINDOW_OPTS = {
  size = function() return 560, 380 end,
  resizable = { 400, 280 },
  keepTitle = true,
  place = function(sw, sh, w, h) return sw / 2 - w / 2, sh / 2 - h / 2 - 40 end,
}
local function drawWindow(x, y, w, h, s)
  local _, lh = ui.CalcTextSize("Ag")
  local btnH = lh + 10 * s
  local rows = K.Rows(WINDOW, LIST)
  local all = rows[1] and rows[1].texts or {}
  local won, lost = tonumber(all[2] or ""), tonumber(all[3] or "")
  local rate = (won and lost and won + lost > 0) and string.format("%.0f%%", 100 * won / (won + lost)) or "-"
  K.StatRow({
    { "Points to spend", W.Text(WINDOW, "AdvStats_AvailableValue"), C.pet, "Adventure points you can spend" },
    { "Wins", all[2] or "-", C.text },
    { "Losses", all[3] or "-", C.text },
    { "Win rate", rate, C.gold },
    { "Points earned", all[6] or "-", C.accent, "Adventure points earned in all themes" },
  }, 96)
  ui.Dummy(0, 4 * s)
  K.Heading("By theme", "click a header to sort")
  W.List(WINDOW, LIST, 0, -(btnH + 8 * s), {
    sortable = true,
    columns = { { "Theme", 0 }, { "Wins", 60 }, { "Losses", 60 }, { "Rank", 56 }, { "Rank %", 60 }, { "Points", 60 } },
    filter = function(row) return row ~= 0 end, -- the totals are the tiles
  })
  W.ButtonRow(WINDOW, {
    { "AdvStats_RefreshButton", "Refresh", { tooltip = "Ask the server for your latest record" } },
    { "AdvStats_LeaderboardButton", "Leaderboard", { tooltip = "The best adventurers by theme" } },
    { "AdvStats_DoneButton", "Done" },
  }, btnH)
end

ui.window("Adventure Stats", function()
  if not eq.InGame() then return end
  hud.Dialog(WINDOW, "Adventure Record", WINDOW_OPTS, drawWindow)
end)
