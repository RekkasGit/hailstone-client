-- Pet (gamepad pack): the stock pet window (PetInfoWindow) opened up. The HUD's pet bar is that window folded (it
-- shows whenever the game shows PetInfoWindow, as it does when a pet is summoned); this window is the same stock
-- window with its command buttons, from the main menu's Pet tile, or when the pet window key / EQ menu shows it
-- while there is a pet already. Closing it (B) folds it back to the HUD bar.
--   the pet's HP (A targets the pet, the stock gauge's click), the pet's target
--   on a server with several pets (NMS) the other pets' HP (the window's Pet2HPGauge / Pet3HPGauge; A targets
--   that pet)
--   the command buttons (Pet0_Button..: attack, follow, guard, taunt, sit, stop, regroup, back off, get lost):
--     A sends the command (the stock button's click: /pet words); a lit button is a state that is on; X puts the
--     command on a hot button (the slot picker, lib/assign.lua): the stock window's drag onto a bar, "Pet <command>";
--     Y picks the command a button holds (the stock button's right-click menu: hold, ghold, spellhold, focus, ...
--     as the pet and the character's AAs allow, or none)
--   the pet's buffs, with their time left
-- The window is lib/pet.lua, which a plugin's Pet window can build on.
local P = dofile("lib/pet.lua")

ui.window("Pet", function() P.window() end)
