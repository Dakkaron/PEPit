SetTextSize(3)
SetTextDatum(1)
position = 1
for i = 1, #(Competitors.name) do
  if (Competitors.distance[i] > Distance) then
    position = position + 1
  end
end

DrawString(position .. ". Platz", 160, 30)

SetTextSize(2)
SetTextDatum(0)
for i = 1, #(Competitors.name) do
  if i>=position then
    cPos = i + 1
  else
    cPos = i
  end
  DrawString(cPos .. ". ", 30, 50 + (cPos)*20)
  DrawString(Competitors.name[i], 60, 50 + (cPos)*20)
end

SetTextColor(0xffc0)
DrawString(position .. ". ", 30, 50 + (position)*20)
DrawString("Du", 60, 50 + (position)*20)
SetTextColor(0xffff)