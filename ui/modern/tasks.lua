-- Quest journal: replaces the game's TaskWnd. Two pages, the game's own (the window's tab box):
--   * Active: the tasks at the left as cards (title, time left; a search box filters them), the selected one at the
--     right: its kind, a timer bar for a timed task, the objectives with a tick and a progress bar each (the hidden
--     later steps folded into one line) and the description with its links and the reward (the game's text);
--     Abandon at the bottom (the game asks first), the request timer under the list.
--   * History: the completed tasks, searchable; a click asks the server for that one's objectives.
-- Everything is the game's: the rows are TASK_TaskList / TASK_TaskElementList / QUEST_* (eq.ListRow), a card click
-- selects in the original (eq.ListClick), the description is TASK_Description, the kind and timer the
-- model's TASK_Summary (app/lua_eq_progress.cpp), Abandon the original's TASK_AbandonButton. The Shared Task page,
-- Monster Select and Preview Reward are not built by the client (it has no reward selection window), so they are
-- left out.
local hud = dofile("lib/hud.lua")
local W = dofile("lib/widgets.lua")
local K = dofile("lib/quests_kit.lua")
local T = hud.T
local C = T.color
local WINDOW = "TaskWnd"
local TABS = "TaskSubwindows"
local PAGE_ACTIVE, PAGE_HISTORY = 0, 2

local state = { listW = 250 }
local KIND = { ["0"] = { "Task", C.accent }, ["1"] = { "Shared task", C.xp }, ["2"] = { "Quest", C.gold } }

-- "3/5" -> 3, 5
local function counts(status)
  local d, g = (status or ""):match("(%d+)%s*/%s*(%d+)")
  return tonumber(d), tonumber(g)
end

-- The stock lists' blank rows between their groups (CTaskWnd's separators).
local function separator(r)
  for _, t in ipairs(r.texts) do if t ~= "" then return false end end
  return true
end

-- The time left column: "hh:mm:ss", or the game's "Unlimited" for no limit.
local function timed(left) return left ~= "" and left:match("^%d+:%d%d:%d%d$") ~= nil end

-- ---------------------------------------------------------------- the list (left)
local function taskList(listId, rows, selected, needle, s, emptyText, emptyHint, titleCol, timeCol)
  local shown = 0
  local listed = 0
  for _, r in ipairs(rows) do
    local title = r.texts[titleCol] or ""
    if not separator(r) then listed = listed + 1 end
    if not separator(r) and (needle == "" or title:lower():find(needle, 1, true)) then
      shown = shown + 1
      local left = timeCol and r.texts[timeCol] or ""
      if not timed(left) then left = "" end
      local out = left:match("^0+:00:00$") ~= nil
      local clicked = K.Card(listId .. r.row, r.row == selected, left ~= "" and 44 or 30, function(x, y, w, h)
        local _, th = T.TextSize("Ag", T.font.normal)
        local ty = left ~= "" and y + 6 * s or y + (h - th) / 2
        T.Text(x, ty, T.Fit(title, w - 8 * s, T.font.normal), C.text)
        if left ~= "" then
          T.Text(x, ty + th + 2 * s, out and "Out of time" or (left .. " left"), out and C.danger or C.muted, T.font.small)
        end
      end)
      if ui.IsItemHovered() and T.TextSize(title) > ui.GetContentRegionAvail() - 20 * s then ui.SetTooltip(title) end
      if clicked then eq.ListClick(WINDOW, listId, r.row, "left", titleCol - 1) end
    end
  end
  if listed == 0 then K.Empty(emptyText, emptyHint)
  elseif shown == 0 then K.Empty("Nothing matches \"" .. (state.search or "") .. "\"") end
end

-- ---------------------------------------------------------------- objectives
local function objectives(listId, s)
  local rows = K.Rows(WINDOW, listId)
  local visible, hidden, done = {}, 0, 0
  for _, r in ipairs(rows) do
    if separator(r) then -- the blank row between two steps
    elseif (r.texts[1] or ""):match("^[%s%?]*$") then hidden = hidden + 1
    else
      visible[#visible + 1] = r
      if r.texts[2] == "Done" then done = done + 1 end
    end
  end
  if #visible == 0 and hidden == 0 then return false end
  K.Heading("Objectives", string.format("%d of %d done", done, #visible + hidden))
  local tick = 14 * s
  for i, r in ipairs(visible) do
    local d, g = counts(r.texts[2])
    local complete = r.texts[2] == "Done"
    local x, y = ui.GetCursorScreenPos()
    K.Tick(x, y + 2 * s, tick, complete)
    ui.SetCursorScreenPos(x + tick + 8 * s, y)
    ui.BeginGroup()
    ui.PushTextWrapPos(0)
    local c = complete and C.faint or C.text
    ui.TextColored(c[1], c[2], c[3], 1, r.texts[1])
    ui.PopTextWrapPos()
    local zone = r.texts[3] or ""
    -- progress (a bar when there is more than one to do), then where
    local _, lh = ui.CalcTextSize("Ag")
    if g and g > 1 and not complete then
      local bx, by = ui.GetCursorScreenPos()
      local bw = math.min(ui.GetContentRegionAvail() * 0.5, 180 * s)
      T.Bar("task." .. listId .. i, bx, by + (lh - 12 * s) / 2, bx + bw, by + (lh + 12 * s) / 2, s, d / g, C.accent,
        { center = r.texts[2], font = T.font.tiny, noTrail = true })
      ui.Dummy(bw, lh)
      if zone ~= "" then ui.SameLine(0, 10 * s) end
    elseif complete or (g and g > 0) then
      K.Muted(complete and "Done" or r.texts[2], complete and C.pet or C.muted)
      if zone ~= "" then ui.SameLine(0, 10 * s) end
    end
    if zone ~= "" then
      K.Muted(zone == "ALL" and "Anywhere" or zone, C.faint)
      if ui.IsItemHovered() then ui.SetTooltip("Where this counts") end
    end
    ui.EndGroup()
    ui.Dummy(0, 4 * s)
  end
  if hidden > 0 then
    local x, y = ui.GetCursorScreenPos()
    K.Tick(x, y + 2 * s, tick, false, { C.ink[1], C.ink[2], C.ink[3], 0.25 })
    ui.SetCursorScreenPos(x + tick + 8 * s, y)
    ui.PushTextWrapPos(0)
    K.Muted(hidden == 1 and "1 more step, revealed as you go" or (hidden .. " more steps, revealed as you go"), C.faint)
    ui.PopTextWrapPos()
  end
  return true
end

-- ---------------------------------------------------------------- the selected task (right)
local function details(s)
  local rows, selected = K.Rows(WINDOW, "TASK_TaskList")
  local task
  for _, r in ipairs(rows) do if r.row == selected then task = r end end
  if not task then
    K.Empty("Select a task to see its objectives.")
    return
  end
  local info = K.Fields(W.Text(WINDOW, "TASK_Summary"))
  ui.PushFont(T.font.large)
  ui.PushTextWrapPos(0)
  ui.TextColored(C.text[1], C.text[2], C.text[3], 1, task.texts[2] or "")
  ui.PopTextWrapPos()
  ui.PopFont()
  local kind = KIND[info.type or ""]
  if kind then K.Chip(kind[1], kind[2]) end
  local dur, left = tonumber(info.duration) or 0, tonumber(info.left) or 0
  if dur > 0 then
    if kind then ui.SameLine(0, 8 * s) end
    -- the time left as a bar that drains (green, then gold, then red) and in words after it
    local x, y = ui.GetCursorScreenPos()
    local label = left > 0 and (K.Time(left) .. " left") or "Out of time"
    local bw = math.max(40 * s, ui.GetContentRegionAvail() - ui.CalcTextSize(label) - 10 * s)
    local _, lh = ui.CalcTextSize("Ag")
    local frac = left / dur
    local col = frac < 0.15 and C.danger or (frac < 0.4 and C.gold or C.pet)
    T.Bar("task.timer", x, y + (lh - 8 * s) / 2, x + bw, y + (lh + 8 * s) / 2, s, frac, col, { noTrail = true })
    ui.Dummy(bw, lh)
    if ui.IsItemHovered() then ui.SetTooltip(K.Time(left) .. " left of " .. K.Time(dur)) end
    ui.SameLine(0, 10 * s)
    ui.TextColored(col[1], col[2], col[3], 1, label)
  elseif kind then
    ui.SameLine(0, 8 * s)
    K.Muted("No time limit", C.faint)
  end
  ui.Dummy(0, 2 * s)
  objectives("TASK_TaskElementList", s)
  local desc = W.Text(WINDOW, "TASK_Description")
  if K.HasText(desc) then
    K.Heading("Description")
    K.Stml(WINDOW, "TASK_Description", desc)
  end
end

-- ---------------------------------------------------------------- pages
local function page(listId, s, w, h, body, opts)
  local listW = math.max(170 * s, math.min(state.listW * s, w * 0.6))
  local footH = opts.footH or 0
  ui.BeginChild("##left" .. listId, listW, h, false)
  local needle = K.Search(state, "search", opts.hint)
  ui.BeginChild("##list" .. listId, 0, -(opts.leftFootH or 0), false)
  local rows, selected = K.Rows(WINDOW, listId)
  taskList(listId, rows, selected, needle, s, opts.emptyText, opts.emptyHint, opts.titleCol, opts.timeCol)
  ui.EndChild()
  if opts.leftFoot then opts.leftFoot() end
  ui.EndChild()
  ui.SameLine(0, 0)
  -- the divider: drag it to give the list more or less room
  local x, y = ui.GetCursorScreenPos()
  T.Hit("##split" .. listId, x + 3 * s, y, x + 11 * s, y + h)
  local hot = ui.IsItemActive() or ui.IsItemHovered()
  if ui.IsItemActive() then
    local mx = ui.GetMousePos()
    local wx = ui.GetWindowPos()
    state.listW = math.max(170, math.min((mx - wx - 10 * s) / s, w * 0.6 / s))
    ui.SetSetting("tasks.listw", tostring(math.floor(state.listW)))
  end
  ui.DrawLine(x + 7 * s, y + 4 * s, x + 7 * s, y + h - 4 * s, C.ink[1], C.ink[2], C.ink[3], hot and 0.30 or 0.07, hot and 2 or 1)
  ui.SetCursorScreenPos(x + 14 * s, y)
  ui.BeginChild("##right" .. listId, 0, h, false)
  ui.BeginChild("##detail" .. listId, 0, -footH, false)
  body()
  ui.EndChild()
  if opts.foot then opts.foot() end
  ui.EndChild()
end

local function abandonFoot(s, btnH)
  return function()
    local avail = ui.GetContentRegionAvail()
    local bw = 130 * s
    ui.SetCursorPos(select(1, ui.GetCursorPos()) + avail - bw, select(2, ui.GetCursorPos()) + 6 * s)
    -- the stock Remove: the game asks "Are you sure you want to quit ...?" itself
    W.Button(WINDOW, "TASK_AbandonButton", "Abandon",
      { danger = true, width = bw, height = btnH, tooltip = "Give up the selected task" })
  end
end

local WINDOW_OPTS = {
  size = function() return 720, 520 end,
  resizable = { 480, 340 },
  place = function(sw, sh, w, h) return sw / 2 - w / 2, sh / 2 - h / 2 - 40 end,
}
local function drawWindow(x, y, w, h, s)
  state.listW = tonumber(ui.GetSetting("tasks.listw")) or state.listW
  -- testing: test.tab.TaskWnd.TaskSubwindows=2 in ui/settings/modern.txt opens the history once
  local testTab = tonumber(ui.GetSetting("test.tab." .. WINDOW .. "." .. TABS) or "")
  if testTab and not state.testTabDone then
    state.testTabDone = true
    eq.SetTabPage(WINDOW, TABS, testTab)
  end
  local current = eq.TabPage(WINDOW, TABS) or 0
  if current ~= PAGE_ACTIVE and current ~= PAGE_HISTORY then eq.SetTabPage(WINDOW, TABS, PAGE_ACTIVE) end
  local pg = W.Tabs(WINDOW, TABS, { { "Active", PAGE_ACTIVE }, { "History", PAGE_HISTORY } })
  local _, lh = ui.CalcTextSize("Ag")
  local btnH = lh + 10 * s
  local cw, bodyH = ui.GetContentRegionAvail() -- (the window's own room: w, h are at least the minimum size)
  if pg == PAGE_HISTORY then
    page("QUEST_TaskList", s, cw, bodyH, function()
      local rows, selected = K.Rows(WINDOW, "QUEST_TaskList")
      local picked
      for _, r in ipairs(rows) do if r.row == selected then picked = r end end
      if not picked then
        K.Empty("Select a completed task to review it.")
        return
      end
      ui.PushFont(T.font.large)
      ui.PushTextWrapPos(0)
      ui.TextColored(C.text[1], C.text[2], C.text[3], 1, picked.texts[2] or "")
      ui.PopTextWrapPos()
      ui.PopFont()
      K.Chip("Completed", C.pet)
      if (picked.texts[3] or "") ~= "" then
        ui.SameLine(0, 8 * s)
        K.Muted(picked.texts[3], C.faint)
      end
      ui.Dummy(0, 2 * s)
      if not objectives("QUEST_TaskElementList", s) then K.Muted("No objectives to show.", C.faint) end
    end, { hint = "Search history", titleCol = 2, emptyText = "No completed tasks yet.",
      emptyHint = "Finished tasks are listed here." })
  else
    local timer = eq.Label(132) or ""
    if timer == "00:00" then timer = "" end -- no request timer running
    local leftFootH = timer ~= "" and (lh + 10 * s) or 0
    page("TASK_TaskList", s, cw, bodyH, function() details(s) end, {
      hint = "Search tasks", titleCol = 2, timeCol = 3,
      emptyText = "No active tasks.", emptyHint = "People around the world have work to offer.",
      footH = btnH + 10 * s, foot = abandonFoot(s, btnH),
      leftFootH = leftFootH,
      leftFoot = timer ~= "" and function()
        ui.Dummy(0, 4 * s)
        K.Muted("Next request in", C.faint)
        ui.SameLine()
        ui.TextColored(C.gold[1], C.gold[2], C.gold[3], 1, timer)
        if ui.IsItemHovered() then ui.SetTooltip("Time before you can be given another task of this kind") end
      end or nil,
    })
  end
end

ui.window("Tasks", function()
  if not eq.InGame() then return end
  hud.Dialog(WINDOW, "Quest Journal", WINDOW_OPTS, drawWindow)
end)
