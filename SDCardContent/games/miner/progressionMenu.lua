SetTextSize(3)
DrawString("Shop", 10, 30)
SetTextSize(2)
DrawString("Geld: $" .. Money, 170, 10)

SetTextSize(2)

if (Stage == 1) then
  if (Money < 2000) then
    FillRect(10, 60, 220, 35, 0xF800)
  else
    FillRect(10, 60, 220, 35, 0x001F)
    SetTextSize(1)
    DrawString("Nicht genug Geld", 15, 84)
  end

  SetTextSize(2)
  DrawString("Tiefer graben: $2000", 15, 65)
  if (Money >= 2000 and IsTouchInZone(10, 60, 220, 35)) then
    Money = Money - 2000
    Stage = 2
    PrefsSetInt("stage", Stage)
    PrefsSetInt("money", Money)
  end
elseif (Stage == 2) then
  if (Money < 10000) then
    FillRect(10, 60, 220, 35, 0xF800)
  else
    FillRect(10, 60, 220, 35, 0x001F)
    SetTextSize(1)
    DrawString("Nicht genug Geld", 15, 84)
  end


  SetTextSize(2)
  DrawString("Tiefer graben: $10000", 15, 65)
  if (Money >= 10000 and IsTouchInZone(10, 60, 220, 35)) then
    Money = Money - 10000
    Stage = 3
    PrefsSetInt("stage", Stage)
    PrefsSetInt("money", Money)
  end
end

SetTextSize(2)
FillRect(220, 200, 100, 40, 0x001F)
DrawString("Fertig", 245, 215)
if (IsTouchInZone(220, 200, 100, 40)) then
  CloseProgressionMenu()
end
