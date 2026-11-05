-- Map (gamepad pack): stands in for MapViewWnd, the large map the minimap opens. The zone's map fitted to a large
-- window, north up (eq.DrawZoneMap: the stock map's lines, labels, group, /find path, compass points and player),
-- following the player until it is panned. A stick pans, LB / RB zoom out / in, Y centres on the player again,
-- X shows / hides the labels, RT turns Auto Z (eq.MapAutoZ: what is well above or below the player fades) on or off;
-- with the mouse, dragging pans and the wheel zooms.
local T = dofile("lib/theme.lua")
local pad = dofile("lib/pad.lua")
local W = dofile("lib/window.lua")
local C = T.C

local MAP = "MapViewWnd"
local ZOOMS = { 1, 1.5, 2.25, 3.4, 5, 7.5, 11 }
local M = {
  open = false,
  zoom = 1,
  follow = true, -- centred on the player
  cx = nil, cy = nil, -- the centre when panned, in map units
  labels = ui.GetSetting("map.labels", "1") == "1",
}

local function setZoom(step)
  M.zoom = math.max(1, math.min(#ZOOMS, M.zoom + step))
end

ui.window("Map", function()
  if not W.shown(MAP) then
    M.open = false
    return
  end
  if not M.open then -- each time it opens: the whole zone, following the player
    M.open, M.zoom, M.follow = true, 1, true
  end
  local zone = eq.ZoneName() or ""
  if W.begin("Map", { w = 1500, h = 960, title = zone ~= "" and zone or "Map",
        hints = { { "lb", "Zoom out" }, { "rb", "Zoom in" }, { "y", "Centre" },
          { "x", M.labels and "Hide labels" or "Labels" }, { "rt", eq.MapAutoZ() and "Auto Z off" or "Auto Z" },
          { "a", eq.FindPathShown() and "End find" or "Find path to centre" }, { "b", "Close" } } }) then
    local x0, y0, x1, y1 = W.area()
    local wx, wy = ui.GetWindowPos()
    ui.SetCursorPos(x0 - wx, y0 - wy)
    ui.InvisibleButton("##maparea", x1 - x0, y1 - y0, ui.ButtonFlags.EnableNav)
    local hovered = ui.IsItemHovered()
    T.rect(x0, y0, x1, y1, C.mapBg, 1, T.px(14))
    T.font(16)
    local cx, cy = M.cx, M.cy
    local k, mx, my, px, py = eq.DrawZoneMap(x0, y0, x1, y1, ZOOMS[M.zoom], cx, cy, M.labels)
    T.unfont()
    T.outline(x0, y0, x1, y1, C.edge, 1, T.px(14), 1)
    if k then
      -- following: the player in the middle once zoomed in (the whole zone at zoom 1)
      if M.follow then
        if M.zoom > 1 and px then M.cx, M.cy = px, py else M.cx, M.cy = mx, my end
      end
      -- panning: the mouse dragging, or a stick (map units per second at full deflection)
      local io = ui.GetIO()
      local dx, dy = 0, 0
      if ui.IsItemActive() and ui.IsMouseDragging(ui.Mouse.Left) then
        dx, dy = -(io.mouseDX or 0) / k, -(io.mouseDY or 0) / k
      end
      if W.focused() and pad.connected() then
        local sx = (pad.axis("lx") or 0) + (pad.axis("rx") or 0)
        local sy = (pad.axis("ly") or 0) + (pad.axis("ry") or 0)
        local speed = (x1 - x0) * 0.8 / k * (1 / math.max(io.fps or 60, 10))
        dx, dy = dx + sx * speed, dy + sy * speed
      end
      if dx ~= 0 or dy ~= 0 then
        M.follow = false
        M.cx, M.cy = (M.cx or mx) + dx, (M.cy or my) + dy
      end
      if hovered and (io.wheel or 0) ~= 0 then setZoom(io.wheel > 0 and 1 or -1) end
    end
    if W.pressed("lb") then setZoom(-1) end
    if W.pressed("rb") then setZoom(1) end
    if W.pressed("y") then M.follow = true end
    -- the find path (the stock map's right click) to the point in the middle, or ending the shown one
    if W.pressed("a") then
      if eq.FindPathShown() then eq.EndFind() elseif M.cx then eq.FindMapLocation(M.cx, M.cy) end
    end
    if W.pressed("rt") then eq.MapAutoZ(not eq.MapAutoZ()) end
    if W.pressed("x") then
      M.labels = not M.labels
      ui.SetSetting("map.labels", M.labels)
    end
    -- the zoom, bottom left
    T.font(17)
    T.draw(x0 + T.px(14), y1 - T.px(32), string.format("x%g%s", ZOOMS[M.zoom], M.follow and "" or "  (panned)"), C.dim)
    T.unfont()
  end
  if W.finish() then eq.ShowWindow(MAP, false) end
end)
