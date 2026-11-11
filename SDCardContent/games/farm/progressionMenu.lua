if not MenuTouch then MenuTouch = {} end
if not MenuLastActionMs then MenuLastActionMs = 0 end
if not MenuSel then MenuSel = 0 end
if not MenuNavMoved then MenuNavMoved = false end
if not MenuJoyWas then MenuJoyWas = false end
if not FarmMenuConfirm then FarmMenuConfirm = false end
if not ConfirmSel then ConfirmSel = 0 end
MenuItems = {}

COL_GOLD = 0xEB2B
COL_GRAY = 0x69AD

function Button(id, x, y, w, h, label, focusable, action)
  FillRect(x, y, w, h, focusable and COL_GOLD or COL_GRAY)
  SetTextColor(0x0000)
  SetTextSize(1)
  DrawString(label, x + 4, y + 4)
  if not focusable then
    return
  end
  local idx = #MenuItems + 1
  MenuItems[idx] = { x = x, y = y, w = w, h = h, act = action }
  local touched = IsTouchInZone(x, y, w, h)
  if touched and not MenuTouch[id] then
    MenuTouch[id] = true
    if Ms - MenuLastActionMs > 400 then
      MenuLastActionMs = Ms
      if FarmMenuConfirm then
        ConfirmSel = idx - 1
      else
        MenuSel = idx - 1
      end
      action()
    end
  elseif not touched then
    MenuTouch[id] = false
  end
end

function ClearFieldGrid()
  for col = 1, FIELD_GRID_COLS do
    for row = 1, FIELD_GRID_ROWS do
      FieldGrid[col][row] = { plowed = false, watered = false, plant = FIELD_GRID_PLANT_NONE, growStage = 0 }
    end
  end
  SaveFieldGrid()
  FarmMenuConfirm = false
  ConfirmSel = 0
end

local plowed = 0
local planted = 0
local ready = 0
for col = 1, FIELD_GRID_COLS do
  for row = 1, FIELD_GRID_ROWS do
    fg = FieldGrid[col][row]
    if fg.plowed then
      plowed = plowed + 1
    end
    if fg.plant ~= FIELD_GRID_PLANT_NONE then
      planted = planted + 1
      if fg.growStage == 3 then
        ready = ready + 1
      end
    end
  end
end

FillRect(0, 0, 320, 240, 0x0000)
SetTextSize(3)
SetTextColor(0xFFFF)
DrawString("Bauernleben", 10, 8)
SetTextSize(2)
DrawString("Geld: €" .. Money, 20, 36)
SetTextSize(1)
DrawString("Gepflügt: " .. plowed .. "/" .. FIELD_GRID_COLS*FIELD_GRID_ROWS, 20, 60)
DrawString("Bebaut: " .. planted, 20, 74)
DrawString("Erntereif: " .. ready, 20, 88)

local miniX = 20
local miniY = 112
local miniCell = 11
for col = 1, FIELD_GRID_COLS do
  for row = 1, FIELD_GRID_ROWS do
    fg = FieldGrid[col][row]
    local c = COL_GRAY
    if fg.plant ~= FIELD_GRID_PLANT_NONE then
      c = fg.growStage == 3 and 0xFD04 or 0xFFE0
    elseif fg.plowed then
      c = 0x001F
    end
    FillRect(miniX + (col-1)*(miniCell+1), miniY + (row-1)*(miniCell+1), miniCell, miniCell, c)
  end
end

Button("ruesten", 20, 180, 100, 30, "Feld rüsten", not FarmMenuConfirm, function()
  FarmMenuConfirm = true
  ConfirmSel = 0
end)
Button("fertig", 240, 200, 80, 40, "Fertig", not FarmMenuConfirm, function()
  CloseProgressionMenu()
end)

if FarmMenuConfirm then
  FillRect(110, 100, 190, 110, 0x0841)
  DrawRect(108, 98, 194, 114, 0xFFFF)
  SetTextColor(0xFFFF)
  SetTextSize(1)
  DrawString("Feld wirklich rüsten?", 118, 112)
  Button("nein", 140, 160, 60, 30, "Nein", true, function()
    FarmMenuConfirm = false
    ConfirmSel = 0
  end)
  Button("ja", 210, 160, 60, 30, "Ja", true, function()
    ClearFieldGrid()
  end)
end

local n = #MenuItems
if n > 0 then
  local sel = FarmMenuConfirm and ConfirmSel or MenuSel
  if sel >= n then
    sel = 0
  end
  local jx, jy = GetJoystickXY()
  if not MenuNavMoved then
    local dir = 0
    if math.abs(jy) > 0.2 then
      dir = jy > 0 and 1 or -1
    elseif math.abs(jx) > 0.2 then
      dir = jx > 0 and 1 or -1
    end
    if dir ~= 0 then
      sel = (sel + dir) % n
      if FarmMenuConfirm then
        ConfirmSel = sel
      else
        MenuSel = sel
      end
      MenuNavMoved = true
    end
  elseif math.abs(jx) <= 0.2 and math.abs(jy) <= 0.2 then
    MenuNavMoved = false
  end
  local it = MenuItems[sel + 1]
  DrawRect(it.x - 2, it.y - 2, it.w + 4, it.h + 4, 0xFFFF)
  local joyBtn = GetJoystickButton()
  if joyBtn and not MenuJoyWas then
    it.act()
  end
  MenuJoyWas = joyBtn
end
