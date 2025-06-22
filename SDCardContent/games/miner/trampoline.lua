if (WorldMapArray[3][1] ~= 0) then -- init
  for x = 0, 7 do
    for y = 0, 4 do
      WorldMapArray[x][y] = 1
    end
  end
  for x = 3, 4 do
    for y = 0, 2 do
      WorldMapArray[x][y] = 0
    end
  end
end

if (NewRepetition) then
  MineTile(3, 3)
  MineTile(4, 3)
end

if (LastJumpMs + 200 > Ms) then
  JumpOffset = (Ms-LastJumpMs) / 8
  YDigOffset = (Ms-LastJumpMs) / 5
elseif (YDigOffset > 0) then
  for x = 0, 7 do
    for y = 0, 4 do
      WorldMapArray[x][y] = WorldMapArray[x][y+1]
    end
    GenTile(x, 5, 2000)
  end
  YDigOffset = 0
else
  YDigOffset = 0
  JumpOffset = 25
end

for x = 0, 7 do
  for y = 0, 4 do
    DrawTile(x, y, YDigOffset, true)
  end
end

if (YDigOffset > 0) then
  for x = 0, 7 do
    DrawTile(x, 5, YDigOffset)
  end
  FillRect(0, 176, 320, 24+40 - YDigOffset, 0x0)
else
  FillRect(0, 176, 320, 24, 0x0)
end

DrawSprite(SBigDigger, 159-SpriteWidth(SBigDigger)/2, 85-SpriteHeight(SBigDigger)/2 - JumpOffset + YDigOffset)

DisplayEarnings(160, 80)
SetTextSize(2)
DrawString("$" .. Money, 190, 188)
