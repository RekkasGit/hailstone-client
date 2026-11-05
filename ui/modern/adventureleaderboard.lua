-- Adventure leaderboard: replaces the game's AdventureLeaderboardWnd. The theme and the order as drop-downs (the
-- original's combo boxes: a pick asks the server for that board), your own standing as tiles, the top adventurers
-- as a sortable table with a name search (your row in gold), Refresh / Done (lib/quests_board.lua). All of it the
-- original's AdvLeaderboard_* children.
local hud = dofile("lib/hud.lua")
local B = dofile("lib/quests_board.lua")
local WINDOW = "AdventureLeaderboardWnd"

local WINDOW_OPTS = {
  size = function() return 480, 500 end,
  resizable = { 360, 360 },
  keepTitle = true,
  place = function(sw, sh, w, h) return sw / 2 - w / 2 + 120, sh / 2 - h / 2 - 20 end,
}
local function drawWindow(x, y, w, h, s)
  B.Draw({
    s = s, win = WINDOW, list = "AdvLeaderboard_LeaderList", nameCol = 2,
    combos = {
      { "AdvLeaderboard_SelectCombobox", "Theme" },
      { "AdvLeaderboard_RiskCombobox", "Order", "Rank by the most wins, or by the best win rate" },
    },
    meName = "AdvLeaderboard_YourNameLabel",
    you = {
      { "Rank", "AdvLeaderboard_YourRankLabel" },
      { "Wins", "AdvLeaderboard_YourSuccessLabel" },
      { "Losses", "AdvLeaderboard_YourFailureLabel" },
      { "Win rate", "AdvLeaderboard_YourPercentLabel", hud.T.color.gold },
    },
    refresh = "AdvLeaderboard_RefreshButton", done = "AdvLeaderboard_DoneButton",
    empty = "No adventurers on this board yet.",
  })
end

ui.window("Adventure Leaderboard", function()
  if not eq.InGame() then return end
  hud.Dialog(WINDOW, "Adventure Leaderboard", WINDOW_OPTS, drawWindow)
end)
