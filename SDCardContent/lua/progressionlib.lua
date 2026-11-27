local C_BG = 0x0000
local C_TEXT = 0xFFFF
local C_FIELD = 0x001F
local C_OK = 0x07E0
local C_LOCK = 0xF800
local C_DIM = 0x2222
local C_TAB = 0x5FC0
local C_ACTIVE = 0xFFE0
local C_OWNED = 0x7BEF

local HEAD_X = 110
local ROW_X = 10
local ROW_W = 220
local ROW_H = 40
local ROW_STEP = 44
local ROW_TOP = 100
local VISIBLE = 3
local TAB_Y = 70
local TAB_H = 26
local TAB_W = 68
local STAT_ROWS = 14
local STAT_STEP = 10

function ProgNum(n)
  n = math.floor(n + 0.5)
  if n < 1000 then return tostring(n) end
  local scales = { { 1000000000000000, "Bill." }, { 1000000000000, "Bio." }, { 100000000000000, "Mrd." }, { 1000000000000, "Mio." }, { 1000000, "Tsd." } }
  for i = 1, #scales do
    local d = scales[i][1]
    if n >= d then
      local v = n / d
      local s
      if v < 10 then
        s = string.format("%.1f", v)
      else
        s = string.format("%.0f", v)
      end
      return string.gsub(s, "%.", ",") .. " " .. scales[i][2]
    end
  end
  return tostring(n)
end

function ProgKey(k)
  return ProgCatalog.prefix .. k
end

function ProgInit(catalog)
  ProgCatalog = catalog
  if not ProgCatalog.prefix then ProgCatalog.prefix = "prog_" end
  ProgState = { balance = 0, lvl = {}, bits = {}, sel = {} }
  ProgState.balance = tonumber(PrefsGetString(ProgKey("balance"), "0")) or 0
  for i = 1, #ProgCatalog.tracks do
    local t = ProgCatalog.tracks[i]
    ProgState.lvl[t.id] = PrefsGetInt(ProgKey("lvl_" .. t.id), 0)
  end
  for i = 1, #ProgCatalog.groups do
    local g = ProgCatalog.groups[i]
    ProgState.bits[g.id] = PrefsGetInt(ProgKey("unl_" .. g.id), g.defaultUnlocked or 1)
    ProgState.sel[g.id] = PrefsGetInt(ProgKey("sel_" .. g.id), 1)
  end
  ProgTab = 0
  ProgSel = 0
  ProgOff = 0
  ProgConfirm = false
  ProgConfirmKind = 0
  ProgConfirmIdx = 0
  ProgConfirmTrack = nil
  ProgConfirmGroup = nil
  ProgItems = {}
  ProgTouch = {}
  ProgNavMoved = false
  ProgJoyWas = false
  ProgLock = false
end

function ProgSave()
  PrefsSetString(ProgKey("balance"), string.format("%d", math.floor(ProgState.balance)))
  for i = 1, #ProgCatalog.tracks do
    local t = ProgCatalog.tracks[i]
    PrefsSetInt(ProgKey("lvl_" .. t.id), ProgState.lvl[t.id])
  end
  for i = 1, #ProgCatalog.groups do
    local g = ProgCatalog.groups[i]
    PrefsSetInt(ProgKey("unl_" .. g.id), ProgState.bits[g.id])
    PrefsSetInt(ProgKey("sel_" .. g.id), ProgState.sel[g.id])
  end
end

function ProgTrack(id)
  for i = 1, #ProgCatalog.tracks do
    if ProgCatalog.tracks[i].id == id then return ProgCatalog.tracks[i] end
  end
  return nil
end

function ProgGroup(id)
  for i = 1, #ProgCatalog.groups do
    if ProgCatalog.groups[i].id == id then return ProgCatalog.groups[i] end
  end
  return nil
end

function ProgGroupItem(gid, idx)
  local g = ProgGroup(gid)
  if not g then return nil end
  return g.items[idx]
end

function ProgBalance()
  return ProgState.balance
end

function ProgLevel(id)
  return ProgState.lvl[id] or 0
end

function ProgGroupBits(g)
  return ProgState.bits[g.id]
end

function ProgGroupUnlocked(gid, idx)
  local g = ProgGroup(gid)
  if not g then return false end
  if idx == 1 then return true end
  return (ProgGroupBits(g) >> (idx - 1)) & 1 == 1
end

function ProgGroupSelected(gid)
  return ProgState.sel[gid] or 1
end

function ProgReq(obj, lvl)
  local req = obj.requires or obj.req
  if not req then return true end
  if type(req) == "function" then return req(lvl) end
  for i = 1, #req do
    if ProgLevel(req[i].id) < req[i].level then return false end
  end
  return true
end

function ProgReqText(obj)
  local req = obj.requires or obj.req
  if not req or type(req) == "function" then return "" end
  local parts = {}
  for i = 1, #req do
    local t = ProgTrack(req[i].id)
    table.insert(parts, (t and t.name or req[i].id) .. " " .. req[i].level)
  end
  return table.concat(parts, ", ")
end

function ProgPrice(track)
  if track.price then return track.price(ProgLevel(track.id)) end
  return math.floor(track.base * (track.growth ^ ProgLevel(track.id)))
end

function ProgAdd(n)
  n = math.floor(n)
  if n > 0 then ProgState.balance = ProgState.balance + n end
end

function ProgSpend(n)
  n = math.floor(n)
  if n > 0 then ProgState.balance = ProgState.balance - n end
end

function ProgBuyTrack(track)
  local lvl = ProgLevel(track.id)
  local price = ProgPrice(track)
  if lvl >= track.max or not ProgReq(track, lvl) or ProgBalance() < price then return false end
  ProgSpend(price)
  ProgState.lvl[track.id] = lvl + 1
  if track.onBuy then track.onBuy(lvl + 1, price) end
  ProgSave()
  return true
end

function ProgBuyGroup(g, idx)
  local item = g.items[idx]
  if not item or ProgGroupUnlocked(g.id, idx) or not ProgReq(item, idx) or ProgBalance() < item.cost then return false end
  ProgSpend(item.cost)
  ProgState.bits[g.id] = ProgGroupBits(g) | (1 << (idx - 1))
  ProgState.sel[g.id] = idx
  if g.onUnlock then g.onUnlock(idx, item.cost) end
  ProgSave()
  return true
end

function ProgGroupSelect(gid, idx)
  if ProgGroupUnlocked(gid, idx) then
    ProgState.sel[gid] = idx
    ProgSave()
    return true
  end
  return false
end

function ProgRegister(id, x, y, w, h, act)
  table.insert(ProgItems, { id = id, x = x, y = y, w = w, h = h, act = act })
  local touched = IsTouchInZone(x, y, w, h)
  if touched then
    if not ProgTouch[id] and not ProgLock then
      ProgTouch[id] = true
      act()
    end
  else
    ProgTouch[id] = false
  end
end

function ProgConfirmText(name, cost, verb)
  return "Willst du " .. name .. "\n" .. verb .. "\n\n" .. ProgNum(cost) .. " " .. ProgCatalog.currency .. " kosten?\n \nKontostand: " .. ProgNum(ProgBalance())
end

function ProgOpenConfirm(kind, obj, idx)
  ProgConfirm = true
  ProgLock = true
  ProgConfirmKind = kind
  ProgConfirmIdx = idx
  if kind == 1 then
    ProgConfirmTrack = obj
  else
    ProgConfirmGroup = obj
  end
end

function ProgDrawConfirm()
  FillRect(HEAD_X, 40, 320 - HEAD_X - 2, 152, C_TAB)
  SetTextColor(C_TEXT)
  SetTextSize(2)
  SetTextDatum(0)
  local name
  local cost
  local verb
  if ProgConfirmKind == 1 then
    name = ProgConfirmTrack.name
    cost = ProgPrice(ProgConfirmTrack)
    verb = "auf Stufe " .. (ProgLevel(ProgConfirmTrack.id) + 1) .. " zu kaufen"
  else
    name = ProgConfirmGroup.name .. " " .. ProgConfirmGroup.items[ProgConfirmIdx].name
    cost = ProgConfirmGroup.items[ProgConfirmIdx].cost
    verb = "freizuschalten"
  end
  DrawString(ProgConfirmText(name, cost, verb), HEAD_X + 6, 46)
  FillRect(HEAD_X + 6, 152, 90, 38, C_FIELD)
  DrawString("Ja", HEAD_X + 40, 164)
  FillRect(216, 152, 90, 38, C_FIELD)
  DrawString("Nein", 250, 164)
  ProgRegister("yes", HEAD_X + 6, 152, 90, 38, function()
    if ProgConfirmKind == 1 then
      ProgBuyTrack(ProgConfirmTrack)
    else
      ProgBuyGroup(ProgConfirmGroup, ProgConfirmIdx)
    end
    ProgConfirm = false
    ProgLock = true
  end)
  ProgRegister("no", 216, 152, 90, 38, function()
    ProgConfirm = false
    ProgLock = true
  end)
end

function ProgDrawTrackRow(track, idx, x, y)
  local lvl = ProgLevel(track.id)
  local maxed = lvl >= track.max
  local locked = not ProgReq(track, lvl)
  local price = ProgPrice(track)
  local canbuy = (not maxed) and (not locked) and ProgBalance() >= price
  if locked then
    FillRect(x, y, ROW_W, ROW_H, C_LOCK)
  elseif maxed then
    FillRect(x, y, ROW_W, ROW_H, C_DIM)
  elseif canbuy then
    FillRect(x, y, ROW_W, ROW_H, C_FIELD)
  else
    FillRect(x, y, ROW_W, ROW_H, C_DIM)
  end
  local left = x + 4
  if track.icon then
    track.icon(x + 4, y + 4, idx)
    left = x + 32
  end
  SetTextColor(C_TEXT)
  SetTextSize(2)
  SetTextDatum(0)
  DrawString(track.name .. " " .. lvl .. "/" .. track.max, left, y + 3)
  SetTextSize(1)
  if locked then
    SetTextColor(C_ACTIVE)
    DrawString("Braucht " .. ProgReqText(track), left, y + 25)
  else
    SetTextColor(C_TEXT)
    if track.effect then DrawString(track.effect(lvl), left, y + 25) end
  end
  SetTextSize(2)
  SetTextDatum(2)
  if maxed then
    SetTextColor(C_OK)
    DrawString("max", x + ROW_W - 4, y + 3)
  elseif locked then
    SetTextColor(C_ACTIVE)
    DrawString("", x + ROW_W - 4, y + 3)
  else
    SetTextColor(canbuy and C_OK or C_ACTIVE)
    DrawString(ProgNum(price), x + ROW_W - 4, y + 3)
  end
  SetTextDatum(0)
  if not maxed and not locked then
    ProgRegister("t_" .. track.id, x, y, ROW_W, ROW_H, function()
      ProgOpenConfirm(1, track, idx)
    end)
  end
end

function ProgDrawGroupRow(group, idx, x, y)
  local item = group.items[idx]
  local unlocked = ProgGroupUnlocked(group.id, idx)
  local active = ProgGroupSelected(group.id) == idx
  local ok = ProgReq(item, idx)
  if active then
    FillRect(x, y, ROW_W, ROW_H, C_ACTIVE)
  elseif unlocked then
    FillRect(x, y, ROW_W, ROW_H, C_OWNED)
  elseif ok and ProgBalance() >= item.cost then
    FillRect(x, y, ROW_W, ROW_H, C_FIELD)
  else
    FillRect(x, y, ROW_W, ROW_H, C_LOCK)
  end
  local left = x + 4
  if group.icon then
    group.icon(x + 4, y + 4, item)
    left = x + 32
  end
  SetTextColor(C_TEXT)
  SetTextSize(2)
  SetTextDatum(0)
  DrawString(item.name, left, y + 3)
  SetTextSize(1)
  if ok then
    SetTextColor(C_TEXT)
    if item.info then DrawString(item.info, left, y + 25) end
  else
    SetTextColor(C_ACTIVE)
    DrawString("Braucht " .. ProgReqText(item), left, y + 25)
  end
  SetTextSize(2)
  SetTextDatum(2)
  if active then
    DrawString("Aktiv", x + ROW_W - 4, y + 3)
  elseif unlocked then
    DrawString("Im Besitz", x + ROW_W - 4, y + 3)
  else
    DrawString(ProgNum(item.cost), x + ROW_W - 4, y + 3)
  end
  SetTextDatum(0)
  if unlocked then
    if not active then
      ProgRegister("g_" .. group.id .. "_" .. idx, x, y, ROW_W, ROW_H, function()
        ProgGroupSelect(group.id, idx)
      end)
    end
  elseif ok and ProgBalance() >= item.cost then
    ProgRegister("g_" .. group.id .. "_" .. idx, x, y, ROW_W, ROW_H, function()
      ProgOpenConfirm(2, group, idx)
    end)
  end
end

function ProgDrawScroll(count)
  if count > VISIBLE then
    if ProgOff > 0 then
      FillRect(275, 100, 32, 32, C_DIM)
      DrawLine(291, 106, 283, 118, C_TEXT)
      DrawLine(291, 106, 299, 118, C_TEXT)
      ProgRegister("up", 275, 100, 32, 32, function()
        ProgOff = ProgOff - 1
        if ProgOff < 0 then ProgOff = 0 end
      end)
    end
    if ProgOff < count - VISIBLE then
      FillRect(275, 150, 32, 32, C_DIM)
      DrawLine(283, 158, 291, 170, C_TEXT)
      DrawLine(299, 158, 291, 170, C_TEXT)
      ProgRegister("down", 275, 150, 32, 32, function()
        ProgOff = ProgOff + 1
        if ProgOff > count - VISIBLE then ProgOff = count - VISIBLE end
      end)
    end
  end
end

local ProgLastTick = nil

function ProgTick()
  local d = 0
  if MsDelta then
    d = MsDelta
  else
    local now = Ms or 0
    if ProgLastTick then d = now - ProgLastTick end
    ProgLastTick = now
  end
  if d < 0 then d = 0 end
  if d > 1000 then d = 1000 end
  if ProgCatalog and ProgCatalog.tick then ProgCatalog.tick(d) end
end

function ProgDraw()
  ProgItems = {}
  FillRect(0, 0, 320, 240, C_BG)
  ProgTick()
  if not IsTouchInZone(0, 0, 320, 240) then ProgLock = false end

  SetTextColor(C_TEXT)
  SetTextSize(3)
  SetTextDatum(0)
  DrawString(ProgCatalog.title or "Menü", HEAD_X, 4)
  SetTextColor(C_OK)
  SetTextSize(2)
  DrawString(ProgCatalog.currency, HEAD_X, 32)
  DrawString(ProgNum(ProgBalance()), HEAD_X, 50)
  SetTextColor(C_TEXT)

  local tab = ProgCatalog.tabs[ProgTab + 1]

  if not ProgConfirm then
    for i = 1, #ProgCatalog.tabs do
      local x = HEAD_X + (i - 1) * TAB_W
      FillRect(x, TAB_Y, TAB_W - 4, TAB_H, ProgTab == (i - 1) and C_TAB or C_FIELD)
      SetTextColor(C_TEXT)
      SetTextSize(1)
      SetTextDatum(0)
      DrawString(ProgCatalog.tabs[i].name, x + 4, TAB_Y + 9)
      ProgRegister("tab_" .. i, x, TAB_Y, TAB_W - 4, TAB_H, function()
        ProgTab = i - 1
        ProgOff = 0
        ProgSel = 0
      end)
    end
  end

  if ProgConfirm then
    ProgDrawConfirm()
  elseif tab.kind == "tracks" then
    local idxs = tab.tracks
    if not idxs then
      idxs = {}
      for i = 1, #ProgCatalog.tracks do table.insert(idxs, i) end
    end
    if ProgOff > #idxs - VISIBLE then ProgOff = #idxs - VISIBLE end
    for i = 1, math.min(VISIBLE, #idxs) do
      ProgDrawTrackRow(ProgCatalog.tracks[idxs[ProgOff + i]], ProgOff + i, ROW_X, ROW_TOP + (i - 1) * ROW_STEP)
    end
    ProgDrawScroll(#idxs)
  elseif tab.kind == "group" then
    local g = ProgGroup(tab.group)
    if g then
      if ProgOff > #g.items - VISIBLE then ProgOff = #g.items - VISIBLE end
      for i = 1, math.min(VISIBLE, #g.items) do
        ProgDrawGroupRow(g, ProgOff + i, ROW_X, ROW_TOP + (i - 1) * ROW_STEP)
      end
      ProgDrawScroll(#g.items)
    end
  elseif tab.kind == "stats" then
    SetTextColor(C_TEXT)
    SetTextSize(1)
    SetTextDatum(0)
    local rows = tab.rows()
    for i = 1, math.min(STAT_ROWS, #rows) do
      DrawString(rows[i], ROW_X, ROW_TOP + (i - 1) * STAT_STEP)
    end
  elseif tab.kind == "custom" then
    tab.render()
  end

  FillRect(240, 200, 80, 40, C_FIELD)
  SetTextColor(C_TEXT)
  SetTextSize(2)
  SetTextDatum(0)
  DrawString("Fertig", 255, 215)
  ProgRegister("done", 240, 200, 80, 40, function()
    ProgConfirm = false
    ProgSave()
    CloseProgressionMenu()
  end)

  local n = #ProgItems
  if n > 0 then
    if ProgConfirm and ProgSel >= n then ProgSel = 1 end
    if not ProgConfirm and ProgSel >= n then ProgSel = 0 end
    local jx, jy = GetJoystickXY()
    if not ProgNavMoved then
      local dir = 0
      if math.abs(jy) > 0.2 then
        dir = jy > 0 and 1 or -1
      elseif math.abs(jx) > 0.2 then
        dir = jx > 0 and 1 or -1
      end
      if dir ~= 0 then
        ProgSel = (ProgSel + dir) % n
        ProgNavMoved = true
      end
    elseif math.abs(jx) <= 0.2 and math.abs(jy) <= 0.2 then
      ProgNavMoved = false
    end
    local it = ProgItems[ProgSel + 1]
    if it then
      DrawRect(it.x - 2, it.y - 2, it.w + 4, it.h + 4, C_TEXT)
      local joyBtn = GetJoystickButton()
      if joyBtn and not ProgJoyWas then
        it.act()
      end
      ProgJoyWas = joyBtn
    end
  end
end
