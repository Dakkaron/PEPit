-- Progression menu: plain score recap + "Fertig" (same pattern as the
-- plane game). Runs every frame while the menu is open.
FillRect(0, 0, 320, 240, 0x0000)
SetTextSize(3)
DrawString("Soccer Penalty", 62, 10)
SetTextSize(2)
DrawString("Goals: " .. Goals, 96, 60)
DrawString("Saved: " .. Saves, 104, 84)
if CurrentRepetition then
  DrawString("Reps:    " .. CurrentRepetition, 100, 108)
end

FillRect(240, 200, 90, 40, 0x001F)
SetTextSize(2)
DrawString("Fertig", 255, 215)
if IsTouchInZone(240, 200, 80, 40) then
  CloseProgressionMenu()
end
