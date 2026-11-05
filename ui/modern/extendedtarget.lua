-- Extended targets: replace the game's ExtendedTargetWnd with a compact list at the right. The window is drawn by
-- lib/xtarget.lua (its head says how), so that a plugin's or script's window of the same name can be this one with a
-- part of its own on each row.
local xtarget = dofile("lib/xtarget.lua")

ui.window("Extended Target", function() xtarget.window() end)
