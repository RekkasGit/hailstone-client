-- Server plugins (Hailstone): the question a server's compiled plugins ask before they load (eq.PluginConsent; docs/
-- plugins.md "The server's compiled plugins ask first"). Which server, each plugin's name, file, size and the start of
-- its SHA-256, the warning that a compiled plugin is a program with full access to the computer, and Allow / Don't
-- allow (both kept for those bytes on this server). Its close box puts the question away until the next zone-in. It
-- stands in for the client's own prompt ("PluginConsent"), which stays off while this runs.
local hud = dofile("lib/hud.lua")
local K = dofile("lib/dialogs_kit.lua")
local T = hud.T
local C = T.color
local WINDOW = "PluginConsent"

local function sizeText(n)
  if n >= 1024 * 1024 then return string.format("%.1f MB", n / (1024 * 1024)) end
  if n >= 1024 then return string.format("%.0f KB", n / 1024) end
  return string.format("%d bytes", n)
end

ui.window("Server Plugins", function()
  local q = eq.PluginConsent()
  hud.Dialog(WINDOW, "Allow this server's plugin?", {
    shown = q ~= nil,
    keepTitle = true,
    size = function() return 460, 150 + 38 * (q and #q.plugins or 1) end,
    place = K.Centered,
    onClose = function() eq.AnswerPluginConsent(nil) end,
  }, function(x, y, w, h, s)
    local cx = ui.GetCursorPos()
    ui.PushTextWrapPos(cx + w)
    ui.TextWrapped(string.format("%s wants to run %s on your computer:", q.server,
      #q.plugins == 1 and "a compiled plugin" or "compiled plugins"))
    for _, p in ipairs(q.plugins) do
      ui.BulletText(p.name)
      ui.SameLine()
      ui.TextDisabled(string.format("%s, %s", p.file, sizeText(p.size)))
      ui.Indent(18 * s)
      ui.TextDisabled("sha256 " .. p.short)
      if ui.IsItemHovered() then ui.SetTooltip(p.sha256) end
      ui.Unindent(18 * s)
    end
    ui.Spacing()
    ui.TextColored(C.danger[1], C.danger[2], C.danger[3], 1, q.warning)
    ui.TextDisabled("Your answer is kept for this server. The add-on list on the login screen can change it.")
    ui.PopTextWrapPos()
    ui.Spacing()
    local bw = 150 * s
    if ui.Button("Allow##consent", bw, K.ButtonH(s)) then eq.AnswerPluginConsent(true) end
    ui.SameLine()
    if ui.Button("Don't allow##consent", bw, K.ButtonH(s)) then eq.AnswerPluginConsent(false) end
  end)
end)
