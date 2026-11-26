if not MenuSel then MenuSel = 0 end
if not MenuNavMoved then MenuNavMoved = false end
if not MenuJoyWas then MenuJoyWas = false end
MenuItems = {}

SetTextSize(3)
DrawString("Shop", 70, 5)
SetTextSize(2)
DrawString("Geld: €" .. Money, 170, 10)

SetTextSize(2)

DisplayValidUpgrades()

SetTextSize(2)
FillRect(240, 200, 90, 40, 0x001F)
DrawString("Fertig", 255, 215)
MenuItems[#MenuItems + 1] = { x = 240, y = 200, w = 90, h = 40, act = function() CloseProgressionMenu() end }
if (IsTouchInZone(240, 200, 80, 40)) then
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
