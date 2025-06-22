if IsNewTask then
  RockImageLoaded = -1
end

local targetRockImageNumber = (10*CurrentRepetition) // Repetitions
if RockImageLoaded < targetRockImageNumber then
  if RockImageLoaded >= 0 then
    FreeSprite(SRock)
  end
  RockImageLoaded = targetRockImageNumber
  if RockImageLoaded < 10 then
    SRock = LoadSprite("gfx/rock_" .. RockImageLoaded .. ".bmp", 0, 0x0000)
  end
  if RockImageLoaded > 0 then
    EarnTime = Ms + 1000
    EarnValue = 1
    Money = Money + EarnValue
  end
end

if RockImageLoaded >= 0 and RockImageLoaded<10 then
  DrawSprite(SRock, 210, 160 - SpriteHeight(SRock))
end

DrawSprite(SRobotFunnel, 20, 120)

if (CurrentlyBlowing) then
  local rayWidth = 200 * math.min((Ms-BlowStartMs) / TargetDurationMs)
  local px = 54
  local py = 137
  DrawFastHLine(px, py, rayWidth, 0x0000FF)
  DrawFastHLine(px, py+1, rayWidth, 0x9999FF)
  DrawFastHLine(px, py+2, rayWidth, 0xDDDDFF)
  DrawFastHLine(px, py+3, rayWidth, 0xDDDDFF)
  DrawFastHLine(px, py+4, rayWidth, 0xDDDDFF)
  DrawFastHLine(px, py+5, rayWidth, 0x9999FF)
  DrawFastHLine(px, py+6, rayWidth, 0x0000FF)
end

SetTextSize(2)
if (EarnTime > Ms) then
  DrawAnimSprite(SExplosionAnim, 225, 100, 10 - ((EarnTime - Ms) // 100))
  DrawString("+" .. EarnValue, 265, 40 + (EarnTime - Ms) * 0.01)
end

DrawString("$" .. Money, 190, 188)
SetTextSize(1)

--drawString("Free RAM" .. getFreeRAM(), 10, 100);
