if not MenuSel then MenuSel = 0 end
if not MenuNavMoved then MenuNavMoved = false end
if not MenuJoyWas then MenuJoyWas = false end
MenuItems = {}

local price = 0
if Stage == 1 then
  price = 2000
elseif Stage == 2 then
  price = 5000
elseif Stage == 3 then
  price = 15000
elseif Stage == 4 then
  price = 25000
elseif Stage == 5 then
  price = 35000
elseif Stage == 6 then
  price = 40000
end

SetTextSize(3)
DrawString("Shop", 10, 30)
SetTextSize(2)
DrawString("Geld: €" .. Money, 170, 10)

SetTextSize(2)
if price > 0 then
  if Money < price then
    FillRect(10, 60, 220, 35, 0xF800)
    SetTextSize(1)
    DrawString("Nicht genug Geld", 15, 84)
  else
    FillRect(10, 60, 220, 35, 0x001F)
    MenuItems[1] = { x = 10, y = 60, w = 220, h = 35, act = function()
      Money = Money - price
      Stage = Stage + 1
      PrefsSetInt("stage", Stage)
      PrefsSetInt("money", Money)
    end }
  end
  SetTextSize(2)
  DrawString("Tiefer graben: €" .. price, 15, 65)
end

SetTextSize(2)
FillRect(220, 200, 100, 40, 0x001F)
DrawString("Fertig", 245, 215)
MenuItems[#MenuItems + 1] = { x = 220, y = 200, w = 100, h = 40, act = function() CloseProgressionMenu() end }

if IsTouchInZone(10, 60, 220, 35) and MenuItems[1] then
  MenuSel = 0
  MenuItems[1].act()
elseif IsTouchInZone(220, 200, 100, 40) then
  CloseProgressionMenu()
end

local n = #MenuItems
if MenuSel >= n then MenuSel = 0 end
local jx, jy = GetJoystickXY()
if not MenuNavMoved then
  local dir = 0
  if math.abs(jy) > 0.2 then
    dir = jy > 0 and 1 or -1
  elseif math.abs(jx) > 0.2 then
    dir = jx > 0 and 1 or -1
  end
  if dir ~= 0 then
    MenuSel = (MenuSel + dir) % n
    MenuNavMoved = true
  end
elseif math.abs(jx) <= 0.2 and math.abs(jy) <= 0.2 then
  MenuNavMoved = false
end
local it = MenuItems[MenuSel + 1]
DrawRect(it.x - 2, it.y - 2, it.w + 4, it.h + 4, 0xFFFF)
local joyBtn = GetJoystickButton()
if joyBtn and not MenuJoyWas then
  it.act()
end
MenuJoyWas = joyBtn
