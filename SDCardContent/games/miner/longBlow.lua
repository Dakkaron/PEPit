if (LastBlowStatus == 1 and Ms-BlowEndMs > 0 and Ms-BlowEndMs < 1000) then
  YDigOffset = (Ms - BlowEndMs) / 22
  MineTile(PlayerX, 2)
elseif YDigOffset > 0 then
  for x = 0, 7 do
    for y = 0, 4 do
      WorldMapArray[x][y] = WorldMapArray[x][y+1]
    end
    GenTile(x, 5)
  end
  StrY = StrY - 1
  YDigOffset = 0
  StepsLeft = 2
else
  YDigOffset = 0
end

if (IsTouchInZone(0, 0, 100, 240)) then
  if (YDigOffset == 0 and PlayerX > 0 and not IsCurrentlyTouching and StepsLeft > 0 and WorldMapArray[PlayerX-1][1] ~= 7) then
    IsCurrentlyTouching = true
    PlayerX = PlayerX - 1
    MineTile(PlayerX, 1)
    StepsLeft = StepsLeft - 1
  end
elseif (IsTouchInZone(220, 0, 100, 240)) then
  if (YDigOffset == 0 and PlayerX < 7 and not IsCurrentlyTouching and StepsLeft > 0 and WorldMapArray[PlayerX+1][1] ~= 7) then
    IsCurrentlyTouching = true
    PlayerX = PlayerX + 1
    MineTile(PlayerX, 1)
    StepsLeft = StepsLeft - 1
  end
elseif (IsTouchInZone(110, 0, 40, 53)) then
  if (Money >= 2 and not IsCurrentlyTouching) then
    IsCurrentlyTouching = true
    Money = Money - 2
    EarnTime = Ms + 2000
    EarnValue = -2
    FullVisibilityTime = Ms + 2000
  end
else
  IsCurrentlyTouching = false
end

for x = 0, 7 do
  for y = 0, 4 do
    DrawTile(x, y, YDigOffset, FullVisibilityTime>Ms)
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

if (CurrentlyBlowing) then
  DrawSprite(SRobotDig, PlayerX*40+3 + PlayerXOffset, 1*40 + math.fmod(Ms/100, 2) * 4)
else
  DrawSprite(SRobotIdle, PlayerX*40+3 + PlayerXOffset, 1*40)
end

if (PlayerXOffset > 0) then
  PlayerXOffset = PlayerXOffset - MsDelta * 0.08
elseif (PlayerXOffset < 0) then
  PlayerXOffset = PlayerXOffset + MsDelta * 0.08
end
if (math.abs(PlayerXOffset) < 0.1) then
  PlayerXOffset = 0
end

DisplayEarnings(PlayerX*40 + 20, 40)

if (StepsLeft > 0) then
  if (PlayerX > 0) then
    DrawSprite(SArrowLeft, 3 + math.sin(Ms/200)*2, 100)
  end
  if (PlayerX < 7) then
    DrawSprite(SArrowRight, 291 - math.sin(Ms/200)*2, 100)
  end
end

SetTextSize(2)
DrawString("$" .. Money, 190, 188)
DrawString("Schritte: " .. StepsLeft, 190, 213)

DrawSprite(SItemLantern, 120, 10)
DrawString("$2", 130, 10)
