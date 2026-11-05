-- Pet: replaces the game's PetInfoWindow. The pet's health and its target's, the command buttons (the game's own:
-- their words, and lit while that mode is on: guard, follow, taunt, hold, ...; dragged onto an action bar each makes
-- its hot button, lit there too while on; a right click picks the command a button holds) and the pet's buffs. On a
-- server with several pets (NMS) the other pets' health shows under the pet's (the window's Pet2HPGauge /
-- Pet3HPGauge); a click targets that pet. The window is lib/pet.lua, which a plugin's Pet window can build on.
local pet = dofile("lib/pet.lua")

ui.window("Pet", function() pet.window() end)
