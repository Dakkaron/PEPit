local position = 1
for i = 1, #(Competitors) do
  if (Competitors[i].distance > Distance) then
    position = position + 1
  end
end

if WinScreenPage == 0 then
  SetTextSize(3)
  SetTextDatum(1)

  DrawString(position .. ". Platz", 140, 30)

  SetTextSize(2)
  SetTextDatum(2)
  --DrawString("Punkte", 315, 35)
  for i = 1, #(Competitors) do
    local cPos = 0
    if i>=position then
      cPos = i + 1
    else
      cPos = i
    end
    SetTextDatum(0)
    DrawString(cPos .. ". ", 30, 50 + cPos*20)
    DrawString(CompetitorNames[Competitors[i].nr], 60, 50 + cPos*20)
    SetTextDatum(2)
    --DrawString(8-cPos, 315, 50 + cPos*20)
  end

  SetTextColor(0xffc0)
  SetTextDatum(0)
  DrawString(position .. ". ", 30, 50 + (position)*20)
  DrawString("Du", 60, 50 + (position)*20)
  SetTextDatum(2)
  --DrawString(8-position, 315, 50 + position*20)
  SetTextColor(0xffff)

  SetTextDatum(0)
  DrawString("Preisgeld:", 10, 200)
  DrawString("$" .. (PrizeMoney[position]), 130, 200)
  DrawString("Kontostand:", 10, 220)
  DrawString("$" .. (Money), 130, 220)

  SetTextDatum(2)
  SetTextSize(2)
  FillRect(240, 210, 80, 30, 0x001F)
  DrawString("Fertig", 308, 218)
  if (IsTouchInZone(260, 210, 60, 30)) then
    WinScreenPage = 1
    CloseWinScreen()
  end
else
SetTextSize(2)
  SetTextDatum(1)
  DrawString(CurrentLeague .. ". Liga", 160, 10)

  SetTextSize(1)
  SetTextDatum(0)

  for i = 1, #(LeagueCompetitors) do
    local cPos = 0
    if i>=position then
      cPos = i + 1
    else
      cPos = i
    end
    DrawString(cPos .. ". ", 30, 50 + (cPos)*10)
    DrawString(CompetitorNames[Competitors[i].nr], 60, 50 + (cPos)*10)
  end

end
