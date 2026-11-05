-- Target of target: in this UI the target's target is part of the target frame (target.lua: its name and health
-- under the target's), so this keeps the game's TargetOfTargetWindow from being drawn on its own as well (the game
-- still keeps it, so switching back to Classic finds it as it was).
ui.window("Target of Target", function()
  if not eq.InGame() then return end
  eq.WindowShown("TargetOfTargetWindow")
end)
