local position = 1
for i = 1, #(Competitors) do
  if (Competitors[i].distance > Distance) then
    position = position + 1
  end
end

Money = Money + PrizeMoney[position]
PrefsSetInt("money", Money)
