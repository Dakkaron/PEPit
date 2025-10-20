SetTextSize(3)
DrawString("Shop", 70, 5)
SetTextSize(2)
DrawString("Geld: $" .. Money, 170, 10)

SetTextSize(2)

DisplayValidUpgrades()

SetTextSize(2)
FillRect(240, 200, 80, 40, 0x001F)
DrawString("Fertig", 255, 215)
if (IsTouchInZone(240, 200, 80, 40)) then
  CloseProgressionMenu()
end
