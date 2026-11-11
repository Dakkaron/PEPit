if not MenuSel then MenuSel = 0 end
if not MenuNavMoved then MenuNavMoved = false end
if not MenuJoyWas then MenuJoyWas = false end
MenuItems = {}

SetTextSize(2)
DrawString("Geld:", 235, 35)
DrawString("€" .. Money, 235, 55)

if not ConfirmDialogOpen then
  SetTextSize(3)
  FillRect(75, 2, 100, 28, ShopTab == 0 and 0x5fc0 or 0x001F)
  DrawString("Pferde", 80, 5)
  FillRect(185, 2, 115, 28, ShopTab == 1 and 0x5fc0 or 0x001F)
  DrawString("Anderes", 190, 5)

  MenuItems[1] = { x = 75, y = 2, w = 100, h = 28, act = function() ShopTab = 0 end }
  MenuItems[2] = { x = 185, y = 2, w = 115, h = 28, act = function() ShopTab = 1 end }

  if IsTouchInZone(75, 0, 100, 30) then
    ShopTab = 0
    MenuSel = 0
  elseif IsTouchInZone(185, 0, 115, 30) then
    ShopTab = 1
    MenuSel = 1
  end
end

SetTextSize(2)

if ConfirmDialogOpen then
  DisplayConfirmDialog()
elseif ShopTab == 0 then
  DisplayHorseShop()
else
  DisplayItemShop()
end

SetTextSize(2)
FillRect(240, 200, 80, 40, 0x001F)
DrawString("Fertig", 255, 215)
MenuItems[#MenuItems + 1] = { x = 240, y = 200, w = 80, h = 40, act = function() CloseProgressionMenu() end }
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
