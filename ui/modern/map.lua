-- Map: replaces the game's MapViewWnd (opened and closed by the game: the map key, the minimap, the EQ menu). The
-- zone's map in a resizable window (eq.DrawZoneMap: the stock map's lines and labels, the group, a /find path, the
-- player; north up), with:
--   * zoom with the wheel (around the cursor) or the buttons, Fit (the whole zone), Follow (centred on you);
--   * dragging pans, a double click centres there;
--   * markers of your own, kept per zone (ui/settings/modern.txt, map.markers.<zone>): right-click the map to put
--     one there, or "Mark my spot"; the list at the right centres on one, renames or removes it;
--   * Auto Z (eq.MapAutoZ): lines and labels well above or below you fade, here and on the minimap;
--   * the cursor's location (as /loc gives it) and its distance from you, under the map.
-- The map's own editing (lines, labels, layers) is the classic map toolbar's: "Edit" opens it.
local hud = dofile("lib/hud.lua")
local T = hud.T
local C = T.color
local WINDOW = "MapViewWnd"

local SIDE_W, TOOL_H, STATUS_H = 220, 36, 24
local ZMIN, ZMAX = 1, 40

local M = { zoom = 1, follow = true, cx = nil, cy = nil, open = false, sel = nil, rename = nil, menuAt = nil }

local COLORS = {
  { 0.48, 0.78, 1.00 }, { 1.00, 0.80, 0.36 }, { 0.40, 0.90, 0.55 }, { 1.00, 0.42, 0.42 }, { 0.80, 0.55, 1.00 },
}
local COLOR_NAMES = { "Blue", "Gold", "Green", "Red", "Purple" }
-- the same colours as text colours (made once, not per marker per frame)
local TEXT_COLORS = {}
for k, c in ipairs(COLORS) do TEXT_COLORS[k] = { c[1], c[2], c[3], 1 } end

-- ---------------------------------------------------------------- markers
local function zoneKey() return select(2, eq.ZoneName()) or "" end

-- The markers' setting name for this zone: made again only when the zone changes.
local keyZone, keyName = nil, nil
local function markersKey()
  local zone = zoneKey()
  if zone ~= keyZone then keyZone, keyName = zone, "map.markers." .. zone end
  return keyName
end

local function parseMarkers(raw)
  local list = {}
  for item in raw:gmatch("[^;]+") do
    local name, x, y, c = item:match("^(.-)|(-?[%d.]+)|(-?[%d.]+)|(%d+)$")
    if name then list[#list + 1] = { name = name, x = tonumber(x), y = tonumber(y), color = tonumber(c) } end
  end
  return list
end

-- A fresh list of this zone's markers (the caller may change it and save it).
local function loadMarkers()
  return parseMarkers(ui.GetSetting(markersKey()) or "")
end

-- The list the open window draws: parsed again only when the zone's setting changes (every change to the list is
-- saved at once, so the setting's text is the list).
local drawn = { key = nil, raw = nil, list = nil }
local function drawnMarkers()
  local key = markersKey()
  local raw = ui.GetSetting(key) or ""
  if drawn.list == nil or drawn.key ~= key or drawn.raw ~= raw then
    drawn.key, drawn.raw, drawn.list = key, raw, parseMarkers(raw)
  end
  return drawn.list
end

local function saveMarkers(list)
  local parts = {}
  for _, m in ipairs(list) do
    local name = m.name:gsub("[|;]", " ")
    parts[#parts + 1] = string.format("%s|%.1f|%.1f|%d", name, m.x, m.y, m.color or 1)
  end
  ui.SetSetting(markersKey(), table.concat(parts, ";"))
end

local function addMarker(x, y, name)
  local list = loadMarkers()
  list[#list + 1] = { name = name or ("Marker " .. (#list + 1)), x = x, y = y, color = (#list % #COLORS) + 1 }
  saveMarkers(list)
  return #list
end

-- map units -> /loc (the original's spawn axes are the map's negated: /loc prints Y, X)
local function locOf(mx, my) return -my, -mx end

local function distance(ax, ay, bx, by)
  if not (ax and bx) then return nil end
  return math.sqrt((ax - bx) ^ 2 + (ay - by) ^ 2)
end

-- ---------------------------------------------------------------- pieces
local function toolButton(id, x, y, w, s, label, on, tip)
  local clicked = T.Hit(id, x, y, x + w, y + 26 * s)
  local hov = ui.IsItemHovered()
  local a = C.accent
  if on then
    ui.DrawRectFilled(x, y, x + w, y + 26 * s, a[1], a[2], a[3], 0.22, 6 * s)
    ui.DrawRect(x, y, x + w, y + 26 * s, a[1], a[2], a[3], 0.7, 6 * s, 1)
  else
    ui.DrawRectFilled(x, y, x + w, y + 26 * s, C.ink[1], C.ink[2], C.ink[3], hov and 0.09 or 0.05, 6 * s)
  end
  local _, th = T.TextSize("Ag", T.font.small)
  T.Text(x + w / 2, y + (26 * s - th) / 2, label, (on or hov) and C.text or C.muted, T.font.small, "center")
  if hov and tip then ui.SetTooltip(tip) end
  return clicked
end

local function sidebar(x, y, w, h, s, list, px, py)
  T.Text(x, y, "Markers", C.text, T.font.normal)
  if T.Button("##markme", x, y + 24 * s, x + w, y + 50 * s, s, "Mark my spot", { primary = true, enabled = px ~= nil }) then
    M.sel = addMarker(px, py)
    M.rename = M.sel
  end
  local ly = y + 60 * s
  if #list == 0 then
    T.Text(x, ly, "None yet. Right-click the map", C.faint, T.font.small)
    T.Text(x, ly + 16 * s, "to put one there.", C.faint, T.font.small)
  end
  local wx, wy = ui.GetWindowPos()
  ui.SetCursorPos(x - wx, ly - wy)
  ui.BeginChild("##markers", w, y + h - ly, false)
  local rw = ui.GetContentRegionAvail()
  local _, th = T.TextSize("Ag", T.font.small)
  for i, m in ipairs(list) do
    local rx, ry = ui.GetCursorScreenPos()
    local rh = 40 * s
    ui.InvisibleButton("##mk" .. i, rw, rh)
    local hov, on = ui.IsItemHovered(), M.sel == i
    if on then
      ui.DrawRectFilled(rx, ry, rx + rw, ry + rh, C.accent[1], C.accent[2], C.accent[3], 0.15, 6 * s)
    elseif hov then
      ui.DrawRectFilled(rx, ry, rx + rw, ry + rh, C.ink[1], C.ink[2], C.ink[3], 0.05, 6 * s)
    end
    local col = COLORS[m.color] or COLORS[1]
    ui.DrawCircleFilled(rx + 12 * s, ry + rh / 2, 5 * s, col[1], col[2], col[3], 1)
    T.Text(rx + 24 * s, ry + 4 * s, T.Fit(m.name, rw - 30 * s, T.font.small), C.text, T.font.small)
    local ly2, lx2 = locOf(m.x, m.y)
    local d = distance(m.x, m.y, px, py)
    T.Text(rx + 24 * s, ry + 6 * s + th, string.format("%d, %d%s", math.floor(ly2), math.floor(lx2),
      d and string.format("   %d away", math.floor(d)) or ""), C.faint, T.font.tiny)
    if ui.IsItemClicked(ui.Mouse.Left) then
      M.sel = i
      M.follow, M.cx, M.cy = false, m.x, m.y
      M.zoom = math.max(M.zoom, 3)
    end
    if ui.IsItemClicked(ui.Mouse.Right) then
      M.sel = i
      ui.OpenPopup("##markermenu")
      T.TakeRightClick()
    end
  end
  if ui.BeginPopup("##markermenu") then
    local m = list[M.sel or 0]
    if m then
      ui.TextDisabled(m.name)
      ui.Separator()
      if ui.MenuItem("Rename...") then M.rename = M.sel end
      if ui.BeginMenu("Colour") then
        for k, c in ipairs(COLORS) do
          if ui.MenuItem(COLOR_NAMES[k], m.color == k) then
            m.color = k
            saveMarkers(list)
          end
        end
        ui.EndMenu()
      end
      if ui.MenuItem("Remove") then
        table.remove(list, M.sel)
        saveMarkers(list)
        M.sel = nil
      end
    end
    ui.EndPopup()
  end
  ui.EndChild()
end

-- ---------------------------------------------------------------- window
local OPTS = {
  size = function() return 1000, 680 end,
  resizable = { 520, 360 },
  keepTitle = true,
  place = function(sw, sh, w, h) return sw / 2 - w / 2, sh / 2 - h / 2 end,
}
local function body(x, y, w, h, s)
  local list = drawnMarkers()
  -- the toolbar
  local tx = x
  local zoomOut = toolButton("##zout", tx, y, 30 * s, s, "-", false, "Zoom out")
  tx = tx + 34 * s
  T.Text(tx + 28 * s, y + 6 * s, string.format("x%.1f", M.zoom), C.muted, T.font.small, "center")
  tx = tx + 58 * s
  local zoomIn = toolButton("##zin", tx, y, 30 * s, s, "+", false, "Zoom in")
  tx = tx + 42 * s
  if toolButton("##fit", tx, y, 50 * s, s, "Fit", false, "The whole zone") then M.zoom, M.follow, M.cx, M.cy = 1, false, nil, nil end
  tx = tx + 54 * s
  if toolButton("##follow", tx, y, 70 * s, s, "Follow", M.follow, "Keep yourself in the middle") then M.follow = not M.follow end
  tx = tx + 74 * s
  local labels = ui.GetSetting("map.labels") ~= "0"
  if toolButton("##labels", tx, y, 70 * s, s, "Labels", labels, "The map's labels") then ui.SetSetting("map.labels", labels and "0" or "1") end
  tx = tx + 74 * s
  local showMarks = ui.GetSetting("map.showMarkers") ~= "0"
  if toolButton("##marks", tx, y, 80 * s, s, "Markers", showMarks, "Your markers on the map") then
    ui.SetSetting("map.showMarkers", showMarks and "0" or "1")
  end
  tx = tx + 84 * s
  local autoZ = eq.MapAutoZ()
  if toolButton("##autoz", tx, y, 64 * s, s, "Auto Z", autoZ, "Fade what is above or below you") then eq.MapAutoZ(not autoZ) end
  tx = tx + 68 * s
  if toolButton("##edit", tx, y, 54 * s, s, "Edit", false, "The map toolbar (lines, labels, layers)") then
    eq.ShowWindow("MapToolbarWnd", true)
  end
  local side = ui.GetSetting("map.sidebar") ~= "0"
  if toolButton("##side", x + w - 30 * s, y, 30 * s, s, side and ">" or "<", false, side and "Hide the markers" or "Show the markers") then
    ui.SetSetting("map.sidebar", side and "0" or "1")
  end
  if zoomIn then M.zoom = math.min(ZMAX, M.zoom * 1.5) end
  if zoomOut then M.zoom = math.max(ZMIN, M.zoom / 1.5) end

  -- the map
  local mx0, my0 = x, y + TOOL_H * s
  local mx1 = x + w - (side and (SIDE_W + 12) * s or 0)
  local my1 = y + h - STATUS_H * s
  ui.DrawRectFilled(mx0, my0, mx1, my1, C.mapBg[1], C.mapBg[2], C.mapBg[3], 0.95, 8 * s)
  local wx, wy = ui.GetWindowPos()
  ui.SetCursorPos(mx0 - wx, my0 - wy)
  ui.InvisibleButton("##maparea", math.max(mx1 - mx0, 1), math.max(my1 - my0, 1))
  local hovered, active = ui.IsItemHovered(), ui.IsItemActive()
  ui.PushClipRect(mx0, my0, mx1, my1, true)
  ui.PushFont(T.font.small)
  local k, midx, midy, px, py = eq.DrawZoneMap(mx0, my0, mx1, my1, M.zoom, M.cx, M.cy, labels)
  ui.PopFont()
  local ccx, ccy = (mx0 + mx1) / 2, (my0 + my1) / 2
  local mouseX, mouseY = ui.GetMousePos()
  local cursorMap = nil
  if k and k > 0 then
    if M.follow then
      if px and M.zoom > 1.01 then M.cx, M.cy = px, py else M.cx, M.cy = midx, midy end
    end
    local cx, cy = M.cx or midx, M.cy or midy
    local function toScreen(mx, my) return ccx + (mx - cx) * k, ccy + (my - cy) * k end
    local function toMap(sx, sy) return cx + (sx - ccx) / k, cy + (sy - ccy) / k end
    -- markers
    if showMarks then
      for i, m in ipairs(list) do
        local sx, sy = toScreen(m.x, m.y)
        local col = COLORS[m.color] or COLORS[1]
        ui.DrawCircleFilled(sx + 1, sy + 2, 7 * s, 0, 0, 0, 0.5)
        ui.DrawCircleFilled(sx, sy, 6 * s, col[1], col[2], col[3], 1)
        ui.DrawCircle(sx, sy, 6 * s, C.ink[1], C.ink[2], C.ink[3], M.sel == i and 1 or 0.6, (M.sel == i and 2 or 1) * s)
        T.Text(sx + 9 * s, sy - 8 * s, m.name, TEXT_COLORS[m.color] or TEXT_COLORS[1], T.font.small)
      end
    end
    if hovered then cursorMap = { toMap(mouseX, mouseY) } end
    -- panning
    local io = ui.GetIO()
    if active and ui.IsMouseDragging(ui.Mouse.Left) and ((io.mouseDX or 0) ~= 0 or (io.mouseDY or 0) ~= 0) then
      M.follow = false
      M.cx, M.cy = cx - (io.mouseDX or 0) / k, cy - (io.mouseDY or 0) / k
    end
    -- zoom around the cursor
    if hovered and (io.wheel or 0) ~= 0 then
      local ax, ay = toMap(mouseX, mouseY)
      local z = math.max(ZMIN, math.min(ZMAX, M.zoom * (io.wheel > 0 and 1.25 or 0.8)))
      local k2 = k * z / M.zoom
      M.zoom = z
      if z > ZMIN then
        M.follow = false
        M.cx, M.cy = ax - (mouseX - ccx) / k2, ay - (mouseY - ccy) / k2
      else
        M.cx, M.cy = nil, nil
      end
    end
    if hovered and ui.IsMouseDoubleClicked(ui.Mouse.Left) then
      M.follow = false
      M.cx, M.cy = toMap(mouseX, mouseY)
    end
    if hovered and ui.IsMouseClicked(ui.Mouse.Right) and not ui.GetIO().alt then
      M.menuAt = { toMap(mouseX, mouseY) }
      ui.OpenPopup("##mapmenu")
      T.TakeRightClick()
    end
    if not M.follow and M.zoom <= ZMIN + 0.001 and not M.cx then M.cx, M.cy = nil, nil end
  else
    T.Text(ccx, ccy - 8 * s, "No map for this zone.", C.muted, T.font.small, "center")
  end
  ui.PopClipRect()
  ui.DrawRect(mx0, my0, mx1, my1, C.ink[1], C.ink[2], C.ink[3], 0.08, 8 * s, 1)
  if ui.BeginPopup("##mapmenu") then
    local at = M.menuAt
    if at then
      local ly, lx = locOf(at[1], at[2])
      ui.TextDisabled(string.format("Loc %d, %d", math.floor(ly), math.floor(lx)))
      ui.Separator()
      if ui.MenuItem("Put a marker here") then
        M.sel = addMarker(at[1], at[2])
        M.rename = M.sel
      end
      if ui.MenuItem("Find a path here") then eq.FindMapLocation(at[1], at[2]) end
      if eq.FindPathShown() and ui.MenuItem("End the find path") then eq.EndFind() end
      if ui.MenuItem("Centre here") then M.follow, M.cx, M.cy = false, at[1], at[2] end
      if ui.MenuItem("Back to me") then M.follow = true end
    end
    ui.EndPopup()
  end

  -- the status line: the cursor's location and distance, yours
  local sy = my1 + 5 * s
  if px then
    local ly, lx = locOf(px, py)
    T.Text(mx0 + 4 * s, sy, string.format("You  %d, %d", math.floor(ly), math.floor(lx)), C.muted, T.font.small)
  end
  if cursorMap then
    local ly, lx = locOf(cursorMap[1], cursorMap[2])
    local d = distance(cursorMap[1], cursorMap[2], px, py)
    T.Text(mx1 - 4 * s, sy, string.format("Cursor  %d, %d%s", math.floor(ly), math.floor(lx),
      d and string.format("   %d away", math.floor(d)) or ""), C.muted, T.font.small, "right")
  end

  if side then sidebar(mx1 + 12 * s, my0, SIDE_W * s, my1 - my0, s, list, px, py) end

  -- renaming a marker
  if M.rename then
    ui.OpenPopup("##rename")
    M.renameText = (list[M.rename] or {}).name or ""
    M.renameIndex, M.rename = M.rename, nil
  end
  if ui.BeginPopup("##rename") then
    ui.Text("Marker name")
    ui.SetNextItemWidth(220 * s)
    if ui.IsWindowAppearing and ui.IsWindowAppearing() then ui.SetKeyboardFocusHere() end
    local text, enter = ui.InputText("##markername", M.renameText or "", 48, ui.InputTextFlags.EnterReturnsTrue | ui.InputTextFlags.AutoSelectAll)
    M.renameText = text
    if enter or ui.Button("OK") then
      local l = loadMarkers()
      if l[M.renameIndex] and text:match("%S") then
        l[M.renameIndex].name = text
        saveMarkers(l)
      end
      ui.CloseCurrentPopup()
    end
    ui.EndPopup()
  end
end

ui.window("Map", function()
  if not eq.InGame() then return end
  local shown = eq.WindowShown(WINDOW)
  if not shown then
    M.open = false
    return
  end
  if not M.open then -- each time it opens: the whole zone, following you
    M.open, M.zoom, M.follow = true, 1, true
  end
  local zone = eq.ZoneName() or ""
  hud.Dialog(WINDOW, zone ~= "" and zone or "Map", OPTS, body)
end)
