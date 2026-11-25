-- The structure of popup STML, shared by the two packs' independent renderers.
-- The cells and surrounding text stay as STML for each pack to style and wrap.
local M = {}

local function parseTable(raw, tags, pos)
  local rows, columns = {}, 0
  while true do
    local _, rowOpen = tags:find("^%s*<tr%s*>", pos)
    if not rowOpen then break end
    local row = {}
    pos = rowOpen + 1
    while true do
      local _, cellOpen = tags:find("^%s*<td%s*>", pos)
      if not cellOpen then break end
      local cellClose, after = tags:find("</td%s*>", cellOpen + 1)
      if not cellClose or #row == 64 then return end
      local cellTags = tags:sub(cellOpen + 1, cellClose - 1)
      if cellTags:find("</?table%s*>") or cellTags:find("</?tr%s*>") or cellTags:find("<td%s*>") then return end
      row[#row + 1] = raw:sub(cellOpen + 1, cellClose - 1)
      pos = after + 1
    end
    local _, rowClose = tags:find("^%s*</tr%s*>", pos)
    if not rowClose then return end
    rows[#rows + 1] = row
    columns = math.max(columns, #row)
    pos = rowClose + 1
  end
  local _, tableClose = tags:find("^%s*</table%s*>", pos)
  if columns > 0 and tableClose then
    return { rows = rows, columns = columns }, tableClose + 1
  end
end

function M.parse(raw)
  local tags = raw:lower()
  local blocks, pos, textStart = {}, 1, 1
  while true do
    local first, open = tags:find("<table%s*>", pos)
    if not first then break end
    local block, after = parseTable(raw, tags, open + 1)
    if block then
      local text = raw:sub(textStart, first - 1)
      if text:find("%S") then blocks[#blocks + 1] = { text = text } end
      blocks[#blocks + 1] = block
      textStart = after
    end
    -- Keep incomplete tables in the surrounding text so their contents are still visible.
    pos = after or open + 1
  end
  local text = raw:sub(textStart)
  if #blocks == 0 or text:find("%S") then blocks[#blocks + 1] = { text = text } end
  return blocks
end

return M
