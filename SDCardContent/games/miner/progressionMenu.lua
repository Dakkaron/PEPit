SetTextSize(3)
DrawString("Shop", 10, 30)
SetTextSize(2)
DrawString("Geld: $" .. Money, 170, 10)

SetTextSize(2)

if (Stage == 1) then
  DisplayBuyStage(2000)
elseif (Stage == 2) then
  DisplayBuyStage(5000)
elseif (Stage == 3) then
  DisplayBuyStage(15000)
elseif (Stage == 4) then
  DisplayBuyStage(25000)
elseif (Stage == 5) then
  DisplayBuyStage(35000)
elseif (Stage == 6) then
  DisplayBuyStage(40000)
end

SetTextSize(2)
FillRect(220, 200, 100, 40, 0x001F)
DrawString("Fertig", 245, 215)
if (IsTouchInZone(220, 200, 100, 40)) then
  CloseProgressionMenu()
end
