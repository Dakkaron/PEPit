-- Progression menu: plain recap + "Fertig" (same pattern as soccer/plane).
-- Runs every frame while the menu is open.
FillRect(0, 0, 320, 240, COL_BLACK)
DrawSprite(SBalloon, 20, 90)
SetTextSize(3)
DrawString("Heißluftballon", 62, 10)
SetTextSize(2)
DrawString("Passiert: " .. Passed, 96, 60)
DrawString("Abstürze: " .. Crashes, 96, 84)
if CurrentRepetition then
  DrawString("Wdh.:     " .. CurrentRepetition, 96, 108)
end

FillRect(240, 200, 90, 40, 0x001F)
SetTextSize(2)
DrawString("Fertig", 255, 215)
if IsTouchInZone(240, 200, 80, 40) then
  CloseProgressionMenu()
end
