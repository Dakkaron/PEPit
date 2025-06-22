if (IsTouchInZone(0, 85, 85, 101)) then
  local touchX = GetTouchX() - 35
  local touchY = GetTouchY() - 135
  if touchX == 0 and touchY == 0 then
	touchX = 1
  end
  local length = math.sqrt(touchX * touchX + touchY * touchY)
  local inhalationVectorX = touchX / length
  local inhalationVectorY = touchY / length
  InhalationCrosshairX = 50*inhalationVectorX + 35
  InhalationCrosshairY = 50*inhalationVectorY + 135
end

if (CurrentlyBlowing) then
  local baseEffect = (Ms-BlowStartMs) / 2000.0
  if (not WasBlowing) then
    WasBlowing = true
    TargetVector = {}
    for i = 1, 3 do
      TargetEffectiveness[i] = 100.0 / math.max(1,GetDistancePointLine(35, 135, InhalationCrosshairX, InhalationCrosshairY, TargetPositions[i][1], TargetPositions[i][2]))
      TargetVector[i] = {TargetPositions[i][1] - 35, TargetPositions[i][2] - 135}
      local length = math.sqrt(TargetVector[i][1] * TargetVector[i][1] + TargetVector[i][2] * TargetVector[i][2])
      TargetVector[i][1] = TargetVector[i][1] / length
      TargetVector[i][2] = TargetVector[i][2] / length
    end
  end
  for i = 1, 3 do
    TargetPositions[i][1] = TargetPositions[i][1] - TargetVector[i][1] * baseEffect * TargetEffectiveness[i]
    TargetPositions[i][2] = TargetPositions[i][2] - TargetVector[i][2] * baseEffect * TargetEffectiveness[i]
    if (TargetPositions[i][1] <= 35) then
      TargetPositions[i][1] = 100000
      local r = GetEarnings(TargetType[i])
      EarnValue = EarnValue + r
      Money = Money + r
      EarnTime = Ms + 2000
    end
  end
else
  WasBlowing = false
  for i = 1, 3 do
    if (TargetPositions[i][1] > 500) then
      TargetPositions[i][1] = math.random(100, 300)
      TargetPositions[i][2] = math.random(20, 160)
      local t = math.random(1, 100) * BreathingScore
      Log("Breathing score: " .. BreathingScore)
      Log("t" .. t)
      if (t > 50000) then
        TargetType[i] = 6
      elseif (t > 10000) then
        TargetType[i] = 5
      elseif (t > 5000) then
        TargetType[i] = 4
      elseif (t > 1000) then
        TargetType[i] = 3
      else
        TargetType[i] = 2
      end
    end
  end
end

DrawSprite(SRobotFunnel, 20, 120)

for i = 1, 3 do
  DrawSprite(TypeSprites[TargetType[i]], TargetPositions[i][1] - SpriteWidth(TypeSprites[TargetType[i]])/2, TargetPositions[i][2] - SpriteHeight(TypeSprites[TargetType[i]])/2)
end


DrawCircle(35, 135, 50, 0xFFFF)

SetTextColor(0xFD00)
SetTextSize(2)
DrawString("X", InhalationCrosshairX-4, InhalationCrosshairY-6)
SetTextColor(0xFFFF)
DrawString("$" .. Money, 190, 188)

SetTextColor(0xFFE0)
DisplayEarnings(50, 50)
SetTextColor(0xFFFF)
