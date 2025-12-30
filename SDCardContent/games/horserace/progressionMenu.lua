SetTextSize(2)
DrawString("Geld:", 235, 35)
DrawString("$" .. Money, 235, 55)

if not ConfirmDialogOpen then
  SetTextSize(3)
  FillRect(75, 2, 100, 28, ShopTab == 0 and 0x5fc0 or 0x001F)
  DrawString("Pferde", 80, 5)
  FillRect(185, 2, 115, 28, ShopTab == 1 and 0x5fc0 or 0x001F)
  DrawString("Anderes", 190, 5)
  
  if IsTouchInZone(75, 0, 100, 30) then
    ShopTab = 0
  elseif IsTouchInZone(185, 0, 115, 30) then
    ShopTab = 1
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
if (IsTouchInZone(240, 200, 80, 40)) then
  CloseProgressionMenu()
end
