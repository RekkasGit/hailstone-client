-- Blocked spells: replaces the game's BlockedBuffWnd (the beneficial spells kept from landing on you), drawn by
-- lib/hudx_blocked.lua: a field to add a spell by name or id, the blocked spells as a list with a remove button.
dofile("lib/hudx_blocked.lua")({
  window = "BlockedBuffWnd", title = "Blocked Spells", slot = "BBuff",
  add = "BW_AddSpellToBlock", edit = "BW_EditSpellToBlock", who = "you",
})
