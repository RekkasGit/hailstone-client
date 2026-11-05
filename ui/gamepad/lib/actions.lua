-- Opening windows the way the game does. The pack has no keys of its own: a window is opened by the same path its
-- key or the EQ menu takes, so the game decides what opens (this pack's window, or the C++ one where the pack has
-- none) and nothing behaves differently from the keyboard.
--
-- A.menu(text): runs the EQ menu's entry named `text` (uifiles/default/CascadeMenu.txt: "Inventory",
-- "Alternate Advancement", "Quest Journal", ...), wherever it sits in the menu, as clicking it would: the menu's
-- key command. A.toggle(screen): shows / hides a stock window this pack draws (EqApi::toggle, what its key does).
local A = {}

local function search(level, text, depth)
  local menus = eq.CascadeMenus()
  local m = menus[level]
  if not m then return false end
  for i, item in ipairs(m.items) do
    if item.kind == "command" and item.text == text then
      eq.CascadeClick(level, i)
      return true
    end
  end
  if depth <= 0 then return false end
  for i, item in ipairs(m.items) do
    if item.kind == "submenu" then
      eq.CascadeHover(level, i) -- opens it (and closes the other submenus of this menu)
      if search(level + 1, text, depth - 1) then return true end
    end
  end
  return false
end

-- Runs the EQ menu entry `text`; false when the menu has no such entry.
function A.menu(text)
  if not eq.CascadeMenus then return false end
  local wasOpen = #eq.CascadeMenus() > 0
  if not wasOpen then eq.ClickChild("EQMainWnd", "EQButton", "left") end -- the EQ button opens the menu
  if #eq.CascadeMenus() == 0 then return false end
  local ok = search(1, text, 3)
  if not ok and not wasOpen then eq.CascadeClose() end
  return ok
end

function A.toggle(screen) eq.ShowWindow(screen, not eq.WindowShown(screen)) end

-- The EQ menu entry, else the stock window: for windows of this pack (which the menu also reaches).
function A.open(text, screen)
  if A.menu(text) then return true end
  if screen then
    A.toggle(screen)
    return true
  end
  return false
end

return A
