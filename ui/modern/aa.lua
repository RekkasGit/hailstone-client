-- Alternate Advancement: the window is lib/aa.lua (its head says what it shows), so that a script's window of the
-- same name can be this one with parts of its own.
local aa = dofile("lib/aa.lua")

ui.window("Alternate Advancement", function() aa.window() end)
