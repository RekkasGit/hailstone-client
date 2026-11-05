-- Buffs: replaces the game's BuffWindow with a tray of icons at the top right (lib/bufftray.lua).
local tray = dofile("lib/bufftray.lua")

tray.Register("Buffs", "BuffWindow", 42, {
  icon = 34, perRow = 14,
})
