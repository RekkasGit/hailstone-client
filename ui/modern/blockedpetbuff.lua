-- Blocked pet spells: replaces the game's BlockedPetBuffWnd (the beneficial spells kept from landing on your pet),
-- drawn by lib/hudx_blocked.lua as the player's own list is (blockedbuff.lua).
dofile("lib/hudx_blocked.lua")({
  window = "BlockedPetBuffWnd", title = "Blocked Pet Spells", slot = "BPBuff",
  add = "BW_AddSpellToBlockFromPet", edit = "BW_EditSpellToBlockFromPet", who = "your pet",
})
