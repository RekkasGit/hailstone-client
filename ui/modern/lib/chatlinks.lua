-- Item links in a chat input: the game puts a link in as its tag ("\18" .. type .. 76 hex digits .. name .. "\18");
-- the input shows the name (Collapse, which keeps the tag and where it went in `links`) and the tags go back in when
-- the line is sent (Expand).
local M = {}

-- The wide form (76 digits, 8-digit ids) first, then the stock one (55 digits, 5-digit ids).
local LINK_TAGS = { "\18(%d)(" .. string.rep("%x", 76) .. ")(.-)\18", "\18(%d)(" .. string.rep("%x", 55) .. ")(.-)\18" }

-- text with each link tag as its name; the tags go to the end of `links`, in order, each with where its name is in
-- the input (`at`: the byte it starts on, the text going in after `offset` bytes of it).
function M.Collapse(links, text, offset)
  offset = offset or 0
  local out, n, pos = {}, 0, 1
  while true do
    local best = nil
    for _, pattern in ipairs(LINK_TAGS) do
      local i, j, _, _, name = text:find(pattern, pos)
      if i and (not best or i < best[1]) then best = { i, j, name } end
    end
    if not best then break end
    local i, j, name = best[1], best[2], best[3]
    local plain = text:sub(pos, i - 1)
    out[#out + 1] = plain
    n = n + #plain
    links[#links + 1] = { name = name, tag = text:sub(i, j), at = offset + n + 1 }
    out[#out + 1] = name
    n = n + #name
    pos = j + 1
  end
  out[#out + 1] = text:sub(pos)
  return table.concat(out)
end

-- Where `name` is in text from `from` on, passing over the tags already put back (`spans`) and, with `word`, a match
-- that runs into a letter or digit ("Ring" in "Bring").
local function findName(text, name, from, spans, word)
  while true do
    local i, j = text:find(name, from, true)
    if not i then return nil end
    local inside = nil
    for _, sp in ipairs(spans) do
      if i <= sp[2] and j >= sp[1] then inside = sp; break end
    end
    if inside then
      from = inside[2] + 1
    elseif word and (text:sub(i - 1, i - 1):find("^[%w]") or text:sub(j + 1, j + 1):find("^[%w]")) then
      from = i + 1
    else
      return i, j
    end
  end
end

-- text with each link's name back as its tag. A link whose name is still where it went in (nothing typed before it
-- since) goes back there: a plain word typed earlier with the same name stays plain. Else, the links being in the order
-- they went in, each is looked for after the one before it first (two of the same item go back in their own places),
-- a whole word before a part of one; then anywhere. A name inside a tag already put back is passed over (a tag in a
-- tag is a broken link).
function M.Expand(links, text)
  local spans = {} -- the names that go back as tags: { first, last, tag } in text as it is
  local placed = {}
  for k, link in ipairs(links) do
    local at = link.at
    if at and text:sub(at, at + #link.name - 1) == link.name and findName(text, link.name, at, spans, false) == at then
      placed[k] = { at, at + #link.name - 1, link.tag }
      spans[#spans + 1] = placed[k]
    end
  end
  local after = 1 -- just past the link before
  for k, link in ipairs(links) do
    local sp = placed[k]
    if not sp then
      local i, j = findName(text, link.name, after, spans, true)
      if not i then i, j = findName(text, link.name, after, spans, false) end
      if not i then i, j = findName(text, link.name, 1, spans, true) end
      if not i then i, j = findName(text, link.name, 1, spans, false) end
      if i then
        sp = { i, j, link.tag }
        spans[#spans + 1] = sp
      end
    end
    if sp then after = sp[2] + 1 end
  end
  if #spans == 0 then return text end
  table.sort(spans, function(a, b) return a[1] < b[1] end)
  local out, pos = {}, 1
  for _, sp in ipairs(spans) do
    out[#out + 1] = text:sub(pos, sp[1] - 1)
    out[#out + 1] = sp[3]
    pos = sp[2] + 1
  end
  out[#out + 1] = text:sub(pos)
  return table.concat(out)
end

return M
