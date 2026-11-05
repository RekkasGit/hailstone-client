-- Songs: replaces the game's ShortDurationBuffWindow with a smaller tray under the buffs (lib/bufftray.lua).
local tray = dofile("lib/bufftray.lua")

tray.Register("Songs", "ShortDurationBuffWindow", 20, {
  icon = 26, perRow = 14,
})
